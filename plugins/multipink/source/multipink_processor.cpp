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

    // Parameters — minimal stubs, real implementation in Task 5.
    parameters.addParameter(STR16("Reference"), STR16("dBFS RMS"),
                            kReferenceStepCount - 1, 0,
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsList,
                            kParamReference);
    parameters.addParameter(STR16("Trim"), STR16("dB"), 0, 0.5,
                            ParameterInfo::kCanAutomate, kParamTrim);
    parameters.addParameter(STR16("Mute"), STR16(""), 1, 0,
                            ParameterInfo::kCanAutomate, kParamMute);

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

tresult PLUGIN_API MULTIPINKProcessor::setState(IBStream* /*state*/) { return kResultOk; }
tresult PLUGIN_API MULTIPINKProcessor::getState(IBStream* /*state*/) { return kResultOk; }

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
void MULTIPINKProcessor::resetPinkFilters()   { /* Task 4 */ }
double MULTIPINKProcessor::computeGainLin() const { return 0.0; /* Task 5 */ }
void MULTIPINKProcessor::readParameterChanges(ProcessData&) { /* Task 5 */ }

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
    // 2. (Pink filter — Task 4.)
    // 3. Slot routing — copy claimed slots straight to outputs at unity gain.
    //    Real gain stage comes in Task 5.
    if (claimedStart_ < 0 || claimedChannels_ == 0) {
        for (int c = 0; c < numChannels; ++c)
            std::memset(outputs[c], 0, sizeof(SampleType) * numSamples);
        return;
    }
    int n = std::min(numChannels, claimedChannels_);
    for (int c = 0; c < n; ++c) {
        SampleType* src = scratch.data() + (size_t)(claimedStart_ + c) * numSamples;
        std::memcpy(outputs[c], src, sizeof(SampleType) * numSamples);
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
