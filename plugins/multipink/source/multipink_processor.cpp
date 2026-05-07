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

    // One stereo output bus by default; setBusArrangements may widen it later.
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
        STR16("Mute"), kParamMute, STR16(""),
        0.0, 1.0, 0.0, 1,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

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
    } else {
        if (claimedStart_ >= 0) {
            MultipinkPool::release(claimedStart_, claimedChannels_);
        }
        claimedStart_    = -1;
        claimedChannels_ = 0;
        poolStatus_.store((int)PoolStatus::Unclaimed);
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API MULTIPINKProcessor::setupProcessing(ProcessSetup& setup) {
    maxBlockSize_ = setup.maxSamplesPerBlock;
    scratch32_.assign((size_t)kPoolSize * maxBlockSize_, 0.0f);
    scratch64_.assign((size_t)kPoolSize * maxBlockSize_, 0.0);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API MULTIPINKProcessor::process(ProcessData& data) {
    readParameterChanges(data);
    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;
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
    activeChannels_ = channels;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API MULTIPINKProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = 0; double trim = 0.0; int32 mute = 0; int32 prefStart = -1;
    if (!s.readInt32(refIdx))    return kResultFalse;
    if (!s.readDouble(trim))     return kResultFalse;
    if (!s.readInt32(mute))      return kResultFalse;
    if (!s.readInt32(prefStart)) return kResultFalse;

    paramReferenceIdx_.store(std::clamp<int>(refIdx, 0, kReferenceStepCount - 1));
    paramTrimDb_.store(std::clamp(trim, -6.0, 6.0));
    paramMute_.store(mute ? 1 : 0);
    preferredStart_ = (prefStart >= -1 && prefStart < kPoolSize) ? prefStart : -1;

    // Mirror normalized values into the parameter container so the host
    // and GUI see them on next refresh.
    if (auto* p = parameters.getParameter(kParamReference))
        p->setNormalized((double)paramReferenceIdx_.load() / (kReferenceStepCount - 1));
    if (auto* p = parameters.getParameter(kParamTrim))
        p->setNormalized((paramTrimDb_.load() + 6.0) / 12.0);
    if (auto* p = parameters.getParameter(kParamMute))
        p->setNormalized(paramMute_.load() ? 1.0 : 0.0);

    return kResultOk;
}

tresult PLUGIN_API MULTIPINKProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = paramReferenceIdx_.load();
    double trim  = paramTrimDb_.load();
    int32 mute   = paramMute_.load();
    int32 prefStart = claimedStart_;
    if (!s.writeInt32(refIdx))     return kResultFalse;
    if (!s.writeDouble(trim))      return kResultFalse;
    if (!s.writeInt32(mute))       return kResultFalse;
    if (!s.writeInt32(prefStart))  return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API MULTIPINKProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "multipink.uidesc");
    return nullptr;
}

void MULTIPINKProcessor::seedLCGs() {
    // Per-channel seeding mirrors no.multinoise(64) = noise_env(12345).multinoise(64).
    // Each of the 64 LCG channels is seeded with the master LCG advanced (i+1) times.
    // Bit-identity to Faust to be verified one-off via faust2sndfile reference dump
    // (see plan Task 3.3-3.5); kept as-derived from noises.lib:81 (multirandom).
    uint32_t s = kFaustSeed;
    for (int i = 0; i < kPoolSize; ++i) {
        s = s * 1103515245u + 12345u;
        lcgState_[i] = s;
    }
}
void MULTIPINKProcessor::resetPinkFilters() {
    for (int i = 0; i < kPoolSize; ++i) {
        for (int k = 0; k < 4; ++k) pinkX_[i][k] = 0.0;
        for (int k = 0; k < 3; ++k) pinkY_[i][k] = 0.0;
    }
}
double MULTIPINKProcessor::computeGainLin() const {
    if (paramMute_.load()) return 0.0;
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
            case kParamMute:
                paramMute_.store(v >= 0.5 ? 1 : 0);
                break;
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
    // 2. Pink-shape ALL 64 channels in place (Direct Form I IIR).
    //    y[n] =  b0*x[n] + b1*x[n-1] + b2*x[n-2] + b3*x[n-3]
    //          - a1*y[n-1] - a2*y[n-2] - a3*y[n-3]
    for (int ch = 0; ch < kPoolSize; ++ch) {
        SampleType* row = scratch.data() + (size_t)ch * numSamples;
        double x1 = pinkX_[ch][0], x2 = pinkX_[ch][1], x3 = pinkX_[ch][2], x4 = pinkX_[ch][3];
        double y1 = pinkY_[ch][0], y2 = pinkY_[ch][1], y3 = pinkY_[ch][2];
        for (int s = 0; s < numSamples; ++s) {
            double x0 = (double)row[s];
            double y0 = kPinkB[0]*x0 + kPinkB[1]*x1 + kPinkB[2]*x2 + kPinkB[3]*x3
                      - kPinkA[0]*y1 - kPinkA[1]*y2 - kPinkA[2]*y3;
            row[s] = (SampleType)y0;
            x4 = x3; x3 = x2; x2 = x1; x1 = x0;
            y3 = y2; y2 = y1; y1 = y0;
        }
        pinkX_[ch][0] = x1; pinkX_[ch][1] = x2; pinkX_[ch][2] = x3; pinkX_[ch][3] = x4;
        pinkY_[ch][0] = y1; pinkY_[ch][1] = y2; pinkY_[ch][2] = y3;
    }
    // (void)x4 silences potential unused-warning if optimization is aggressive.
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
               "Instrument|Synth", FULL_VERSION_STR, kVstVersionString,
               Seam::MULTIPINKProcessor::createInstance)
END_FACTORY
