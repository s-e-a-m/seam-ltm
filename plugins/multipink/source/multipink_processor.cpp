#include "multipink_processor.h"
#include "multipink_ids.h"
#include "multipink_pool.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "base/source/fstreamer.h"

#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

MULTIPINKProcessor::MULTIPINKProcessor() {}

tresult PLUGIN_API MULTIPINKProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // A main input bus we never read. It exists precisely so that the signal
    // arriving at the insert DIES here: with zero input buses a host has
    // nothing to hand the plugin, so it routes the track signal AROUND us and
    // the generator appears to "pass audio through" (observed in Nuendo with
    // multipink and ltglide cascaded). Declaring the bus puts us back in the
    // chain, and processBlock() writes or memsets every output channel — so
    // blocking the input is a structural consequence, not a runtime branch.
    //
    // One stereo bus each by default; setBusArrangements may widen them later,
    // and it keeps the two the same width.
    addAudioInput (STR16("Input"),  SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

    auto* refParam = new StringListParameter(
        STR16("Reference"), kParamReference, STR16("dBFS RMS"),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    refParam->appendString(STR16("-23"));
    refParam->appendString(STR16("-20"));
    refParam->appendString(STR16("-18"));
    parameters.addParameter(refParam);

    parameters.addParameter(new RangeParameter(
        STR16("Trim"), kParamTrim, STR16("dB"),
        -6.0, 6.0, 0.0, 0,
        ParameterInfo::kCanAutomate));

    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 1.0, 1,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    auto* stoneParam = new StringListParameter(
        STR16("STONE"), kParamStoneId, STR16(""),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    stoneParam->appendString(STR16("?"));
    stoneParam->appendString(STR16("1"));
    stoneParam->appendString(STR16("2"));
    stoneParam->appendString(STR16("3"));
    stoneParam->appendString(STR16("4"));
    stoneParam->appendString(STR16("5"));
    stoneParam->appendString(STR16("6"));
    stoneParam->appendString(STR16("7"));
    stoneParam->appendString(STR16("8"));
    parameters.addParameter(stoneParam);

    // Read-only display parameters (pushed from process() via outputParameterChanges).
    parameters.addParameter(new RangeParameter(
        STR16("Slot Start"), kParamSlotStart, STR16(""),
        -1.0, 63.0, -1.0, 64,
        ParameterInfo::kIsReadOnly));
    parameters.addParameter(new RangeParameter(
        STR16("Slot Count"), kParamSlotCount, STR16(""),
        0.0, 64.0, 0.0, 64,
        ParameterInfo::kIsReadOnly));
    auto* statusParam = new StringListParameter(
        STR16("Pool Status"), kParamPoolStatus, STR16(""),
        ParameterInfo::kIsReadOnly | ParameterInfo::kIsList);
    statusParam->appendString(STR16("Unclaimed"));
    statusParam->appendString(STR16("OK (preferred)"));
    statusParam->appendString(STR16("OK (first-fit)"));
    statusParam->appendString(STR16("EXHAUSTED"));
    parameters.addParameter(statusParam);

    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API MULTIPINKProcessor::setActive(TBool state) {
    if (state) {
        int actualStart = -1;
        auto r = MultipinkPool::claim(activeChannels_, preferredStart_, actualStart);
        claimedStart_     = actualStart;
        claimedChannels_  = (actualStart >= 0) ? activeChannels_ : 0;
        switch (r) {
            case MultipinkPool::ClaimResult::ClaimedAtPreferred:
                poolStatus_.store((int)PoolStatus::ClaimedAtPreferred); break;
            case MultipinkPool::ClaimResult::ClaimedFirstFit:
                poolStatus_.store((int)PoolStatus::ClaimedFirstFit); break;
            case MultipinkPool::ClaimResult::Exhausted:
                poolStatus_.store((int)PoolStatus::Exhausted); break;
        }
        seedLCGs();
        resetPinkFilters();

        busHandle_ = CalbusClient::instance().registerSlot();
        publishBusRecord();
    } else {
        if (claimedStart_ >= 0) {
            MultipinkPool::release(claimedStart_, claimedChannels_);
        }
        claimedStart_    = -1;
        claimedChannels_ = 0;
        poolStatus_.store((int)PoolStatus::Unclaimed);

        CalbusClient::instance().unregisterSlot(busHandle_);
        busHandle_ = SEAM_CALBUS_NO_HANDLE;
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API MULTIPINKProcessor::setupProcessing(ProcessSetup& setup) {
    maxBlockSize_ = setup.maxSamplesPerBlock;
    scratch32_.assign((size_t)kPoolSize * maxBlockSize_, 0.0f);
    scratch64_.assign((size_t)kPoolSize * maxBlockSize_, 0.0);
    // The filter is a function of the sample rate. setupProcessing is called
    // with the plug-in inactive, so this is the one place it may be designed.
    pinkDesign_.design(setup.sampleRate);
    resetPinkFilters();
    return SingleComponentEffect::setupProcessing(setup);
}

void MULTIPINKProcessor::publishBusRecord() {
    if (busHandle_ == SEAM_CALBUS_NO_HANDLE) return;

    SeamCalbusRecord r{};
    r.kind    = kSeamCalbusPink;
    r.stoneId = (uint32_t)paramStoneId_.load();
    // "active" is the conjunction that exists nowhere else: the pool tracks
    // ownership, so with four instances loaded it reports slots 0/4/8/12 all
    // claimed and cannot say which one is sounding. Level stays a separate
    // field — reference (-23/-20/-18) plus trim (±6) never reaches silence,
    // so an "audible level" test would need a threshold nobody can justify.
    r.active  = (claimedStart_ >= 0 && paramPower_.load() != 0) ? 1u : 0u;
    r.levelDb = kReferenceLevelsDb[paramReferenceIdx_.load()] + paramTrimDb_.load();
    r.sampleRate = processSetup.sampleRate;
    r.u.pink.slotStart = claimedStart_;
    r.u.pink.slotCount = claimedChannels_;

    CalbusClient::instance().publish(busHandle_, r);
}

tresult PLUGIN_API MULTIPINKProcessor::process(ProcessData& data) {
    readParameterChanges(data);

    // Parameter changes arrive here, on the audio thread — hence the seqlock.
    publishBusRecord();

    // Push display state for the GUI (read-only parameters).
    if (auto* outChanges = data.outputParameterChanges) {
        int32 idx;
        auto pushNorm = [&](ParamID id, double v01) {
            auto* q = outChanges->addParameterData(id, idx);
            if (q) { int32 off = 0; q->addPoint(0, v01, off); }
        };
        // Normalize:
        //   slotStart in [-1, 63] -> [0,1]
        //   slotCount in [0,  64] -> [0,1]
        //   poolStatus in [0,  3] -> [0,1]
        pushNorm(kParamSlotStart,  ((double)claimedStart_ + 1.0) / 64.0);
        pushNorm(kParamSlotCount,  (double)claimedChannels_ / 64.0);
        pushNorm(kParamPoolStatus, (double)poolStatus_.load() / 3.0);
    }

    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;

    // A generator is never silent by inheritance. The buffers can arrive still
    // flagged silent from whatever fed the insert, and a host that trusts the
    // flag would skip every plugin downstream of us.
    data.outputs[0].silenceFlags = 0;

    int numChannels = data.outputs[0].numChannels;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32) {
        processBlock<float>((float**)out, numChannels, data.numSamples, scratch32_);
    } else {
        processBlock<double>((double**)out, numChannels, data.numSamples, scratch64_);
    }
    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API MULTIPINKProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns,
    SpeakerArrangement* outs, int32 numOuts) {
    if (numOuts != 1) return kResultFalse;
    int channels = SpeakerArr::getChannelCount(outs[0]);
    if (channels < 1 || channels > kPoolSize) return kResultFalse;
    // The input bus is never read, so the only thing to accept or refuse is a
    // shape we declared: one input as wide as the output (insert on an audio
    // track, where the host uses the same width on both sides) or none at all
    // (instrument track, where the host gives the generator no input).
    if (numIns > 1) return kResultFalse;
    if (numIns == 1 && SpeakerArr::getChannelCount(ins[0]) != channels) return kResultFalse;
    activeChannels_ = channels;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API MULTIPINKProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = 0; double trim = 0.0; int32 power = 0; int32 prefStart = -1;
    if (!s.readInt32(refIdx))    return kResultFalse;
    if (!s.readDouble(trim))     return kResultFalse;
    if (!s.readInt32(power))     return kResultFalse;
    if (!s.readInt32(prefStart)) return kResultFalse;

    paramReferenceIdx_.store(std::clamp<int>(refIdx, 0, kReferenceStepCount - 1));
    paramTrimDb_.store(std::clamp(trim, -6.0, 6.0));
    // Third field is POWER (1 = sounding). It held MUTE, with the
    // opposite meaning, until the 2026-07 UI revision. A session
    // saved MUTED therefore re-opens SOUNDING -- pink noise at the
    // calibration reference, on load. A session saved active
    // re-opens silent. Re-make any preset from before that date.
    //
    // Host automation inverts with it, and nothing on the wire says
    // so: kParamPower is tag 102, the number MUTE used, so an
    // envelope drawn against MUTE now drives POWER and asks for the
    // opposite state at every point on the curve. The tag was kept
    // deliberately -- renumbering would have made the old envelope
    // vanish instead of invert, which is quieter but no safer, since
    // a silent generator is discovered at the next calibration pass
    // and a loud one is discovered by the loudspeaker. Re-draw the
    // envelope along with the presets.
    paramPower_.store(power ? 1 : 0);
    preferredStart_ = (prefStart >= -1 && prefStart < kPoolSize) ? prefStart : -1;

    // stoneId is read last and tolerates absence: presets saved before this
    // parameter existed simply fail the read below. Store unconditionally
    // (0 = undeclared on failure or out-of-range) like every other parameter
    // above, rather than only inside the success branch -- otherwise loading
    // a pre-calbus preset into an instance where the user had set a STONE id
    // would leave that stale id in place, attributing a calibration pass to
    // a loudspeaker the loaded preset never named.
    int32 stoneId = 0;
    paramStoneId_.store(s.readInt32(stoneId) && stoneId >= 0 && stoneId <= 8 ? stoneId : 0);

    // Mirror normalized values into the parameter container so the host
    // and GUI see them on next refresh.
    if (auto* p = parameters.getParameter(kParamReference))
        p->setNormalized((double)paramReferenceIdx_.load() / (kReferenceStepCount - 1));
    if (auto* p = parameters.getParameter(kParamTrim))
        p->setNormalized((paramTrimDb_.load() + 6.0) / 12.0);
    if (auto* p = parameters.getParameter(kParamPower))
        p->setNormalized(paramPower_.load() ? 1.0 : 0.0);
    if (auto* p = parameters.getParameter(kParamStoneId))
        p->setNormalized((double)paramStoneId_.load() / (kStoneIdStepCount - 1));

    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = paramReferenceIdx_.load();
    double trim  = paramTrimDb_.load();
    int32 power  = paramPower_.load();
    int32 prefStart = claimedStart_;
    if (!s.writeInt32(refIdx))     return kResultFalse;
    if (!s.writeDouble(trim))      return kResultFalse;
    if (!s.writeInt32(power))      return kResultFalse;
    if (!s.writeInt32(prefStart))  return kResultFalse;
    if (!s.writeInt32(paramStoneId_.load())) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API MULTIPINKProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "multipink.uidesc");
    return nullptr;
}

void MULTIPINKProcessor::seedLCGs() {
    // Per-channel seeding via splitmix64 avalanche hash, derived from
    // kFaustSeed and the channel index. We do NOT seed sequentially with
    // s = LCG(s) per channel — that would make channel (i+1) a 1-sample
    // shift of channel i, and after the pink IIR (which is time-invariant)
    // the resulting streams would be highly correlated at the pink filter's
    // lag-1 autocorrelation (~0.5-0.7 because of the strong low-frequency
    // content of pink noise). Verified empirically on 2026-05-08: stereo
    // mid-side analysis showed mid > side in violation of the orthogonality
    // expected from independent streams.
    //
    // The fix scrambles each channel's seed to a pseudo-random point in
    // the LCG period (2^32). With 64 channels distributed uniformly over
    // 2^32 states, the probability of any two seeds being within 10k
    // samples is < 1%, so statistical independence is preserved.
    //
    // Faust's no.multinoise(N) achieves the same end via noise_env's
    // internal seed dispersion; this is functionally equivalent though
    // not bit-identical to Faust's stream.
    for (int i = 0; i < kPoolSize; ++i) {
        uint64_t z = (uint64_t)kFaustSeed * 0x9E3779B97F4A7C15ULL
                   + (uint64_t)(i + 1)    * 0xC2B2AE3D27D4EB4FULL;
        z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ULL;
        z ^= z >> 27; z *= 0x94D049BB133111EBULL;
        z ^= z >> 31;
        lcgState_[i] = (uint32_t)z;
    }
}
void MULTIPINKProcessor::resetPinkFilters() {
    for (int s = 0; s < Seam::multipink::PinkDesign::kMaxSections; ++s)
        for (int i = 0; i < kPoolSize; ++i)
            pinkState_[s][i] = 0.0f;
}
double MULTIPINKProcessor::computeGainLin() const {
    if (!paramPower_.load()) return 0.0;
    if (poolStatus_.load() == (int)PoolStatus::Exhausted) return 0.0;
    int idx = paramReferenceIdx_.load();
    if (idx < 0) idx = 0;
    if (idx > kReferenceStepCount - 1) idx = kReferenceStepCount - 1;
    double refDb = kReferenceLevelsDb[idx];
    double trim = paramTrimDb_.load();
    double db = refDb + trim + kCalibrationOffsetDb;
    return std::pow(10.0, db / 20.0);
}

void MULTIPINKProcessor::readParameterChanges(ProcessData& data) {
    auto* changes = data.inputParameterChanges;
    if (!changes) return;
    int32 n = changes->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        auto* q = changes->getParameterData(i);
        if (!q) continue;
        ParamID id = q->getParameterId();
        int32 cnt = q->getPointCount();
        if (cnt <= 0) continue;
        ParamValue v; int32 off;
        if (q->getPoint(cnt - 1, off, v) != kResultOk) continue;
        switch (id) {
            case kParamReference: {
                int idx = (int)std::round(v * (kReferenceStepCount - 1));
                if (idx < 0) idx = 0;
                if (idx > kReferenceStepCount - 1) idx = kReferenceStepCount - 1;
                paramReferenceIdx_.store(idx);
            } break;
            case kParamTrim:
                paramTrimDb_.store(v * 12.0 - 6.0);
                break;
            case kParamPower:
                paramPower_.store(v >= 0.5 ? 1 : 0);
                break;
            case kParamStoneId: {
                int idx = (int)(v * (kStoneIdStepCount - 1) + 0.5);
                if (idx < 0) idx = 0;
                if (idx > kStoneIdStepCount - 1) idx = kStoneIdStepCount - 1;
                paramStoneId_.store(idx);
            } break;
        }
    }
}

template <typename SampleType>
void MULTIPINKProcessor::processBlock(SampleType** outputs, int numChannels,
                                      int numSamples,
                                      std::vector<SampleType>& scratch) {
    // 1. Advance ALL 64 LCGs into scratch[ch * numSamples + s].
    //    (Reason for "all 64": see spec §2.2 — guarantees slot k's stream
    //    is independent of which other slots are active in this instance.)
    for (int ch = 0; ch < kPoolSize; ++ch) {
        uint32_t st = lcgState_[ch];
        SampleType* row = scratch.data() + (size_t)ch * numSamples;
        for (int s = 0; s < numSamples; ++s) {
            st = st * 1103515245u + 12345u;
            row[s] = (SampleType)((int32_t)st / 2147483648.0);
        }
        lcgState_[ch] = st;
    }
    // 2. Pink-shape ALL 64 channels in place: a cascade of first-order
    //    sections, transposed direct form II, one state per section per stream.
    //      y[n] = b0*x[n] + s ; s = b1*x[n] - a1*y[n]
    for (int sec = 0; sec < pinkDesign_.numSections; ++sec) {
        const float b0 = (float)pinkDesign_.b0[sec];
        const float b1 = (float)pinkDesign_.b1[sec];
        const float a1 = (float)pinkDesign_.a1[sec];
        float* state = pinkState_[sec];
        for (int ch = 0; ch < kPoolSize; ++ch) {
            SampleType* row = scratch.data() + (size_t)ch * numSamples;
            float s = state[ch];
            for (int n = 0; n < numSamples; ++n) {
                // Single precision even under kSample64: the filter runs in
                // float at both sample sizes, and the cost measured against a
                // double-state run is 0.0008 dB (multipink_pink_engine_test).
                const float x = (float)row[n];
                const float y = b0 * x + s;
                s = b1 * x - a1 * y;
                row[n] = (SampleType)y;
            }
            state[ch] = s;
        }
    }
    // 3. Slot routing + gain stage.
    if (claimedStart_ < 0 || claimedChannels_ == 0) {
        for (int c = 0; c < numChannels; ++c)
            std::memset(outputs[c], 0, sizeof(SampleType) * numSamples);
        return;
    }
    SampleType g = (SampleType)computeGainLin();
    int n = std::min(numChannels, claimedChannels_);
    for (int c = 0; c < n; ++c) {
        SampleType* src = scratch.data() + (size_t)(claimedStart_ + c) * numSamples;
        SampleType* dst = outputs[c];
        for (int s = 0; s < numSamples; ++s) dst[s] = src[s] * g;
    }
    for (int c = n; c < numChannels; ++c)
        std::memset(outputs[c], 0, sizeof(SampleType) * numSamples);
}

// Explicit template instantiations
template void MULTIPINKProcessor::processBlock<float>(float**, int, int, std::vector<float>&);
template void MULTIPINKProcessor::processBlock<double>(double**, int, int, std::vector<double>&);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::MULTIPINKProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM MULTIPINK", Vst::kDistributable,
               "Fx|Generator", FULL_VERSION_STR, kVstVersionString,
               Seam::MULTIPINKProcessor::createInstance)
END_FACTORY
