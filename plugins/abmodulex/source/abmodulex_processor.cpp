//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "abmodulex_processor.h"
#include "abmodulex_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include <cstring>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

ABMODULEXProcessor::ABMODULEXProcessor() {}

tresult PLUGIN_API ABMODULEXProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;
    addAudioInput (STR16("A-format In"), SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("AmbiX Out"),   SpeakerArr::kAmbi1stOrderACN);
    return kResultOk;
}

tresult PLUGIN_API ABMODULEXProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API ABMODULEXProcessor::process(ProcessData& data) {
    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    int32 numCh = data.inputs[0].numChannels;
    uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    if (data.inputs[0].silenceFlags == getChannelMask(data.inputs[0].numChannels)) {
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        for (int32 i = 0; i < numCh; ++i)
            if (in[i] != out[i]) memset(out[i], 0, bytes);
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;

    if (numCh < 4) {
        for (int32 i = 0; i < numCh; ++i)
            if (in[i] != out[i]) memcpy(out[i], in[i], bytes);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32)
        processBlock<Sample32>(reinterpret_cast<Sample32**>(in),
                               reinterpret_cast<Sample32**>(out), data.numSamples);
    else
        processBlock<Sample64>(reinterpret_cast<Sample64**>(in),
                               reinterpret_cast<Sample64**>(out), data.numSamples);
    return kResultOk;
}

template <typename S>
void ABMODULEXProcessor::processBlock(S** in, S** out, int32 n) {
    for (int32 i = 0; i < n; ++i) {
        double a0,a1,a2,a3;
        abmodulex::encode((double)in[0][i], (double)in[1][i],
                          (double)in[2][i], (double)in[3][i],
                          a0,a1,a2,a3);
        out[0][i] = (S)a0; out[1][i] = (S)a1; out[2][i] = (S)a2; out[3][i] = (S)a3;
    }
}

tresult PLUGIN_API ABMODULEXProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ABMODULEXProcessor::setState(IBStream*) { return kResultOk; }
tresult PLUGIN_API ABMODULEXProcessor::getState(IBStream*) { return kResultOk; }

tresult PLUGIN_API ABMODULEXProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0]) == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API ABMODULEXProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "abmodulex.uidesc");
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::ABMODULEXProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM ABMODULEX",
        0,
        "Fx|Tools",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::ABMODULEXProcessor::createInstance)
END_FACTORY
