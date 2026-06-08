//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · X2UHJ — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "x2uhj_processor.h"
#include "x2uhj_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "x2uhj_readout_view.h"
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

X2UHJProcessor::X2UHJProcessor() {}

tresult PLUGIN_API X2UHJProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;
    // First-order Ambisonics: 4 channels in (AmbiX), 4 channels out (LRTQ).
    addAudioInput (STR16("AmbiX In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("UHJ Out"),   SpeakerArr::kAmbi1stOrderACN);
    return kResultOk;
}

tresult PLUGIN_API X2UHJProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API X2UHJProcessor::setActive(TBool state) {
    if (state) {
        encoder.prepare(processSetup.sampleRate);
        encoder.reset();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API X2UHJProcessor::process(ProcessData& data) {
    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    int32 numCh = data.inputs[0].numChannels;
    uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    // Silent input → silent output. Known limitation (as in the suite's other
    // IIR plugins): the all-pass tail from prior content is not flushed here,
    // so it is suppressed rather than allowed to decay. Acceptable for an
    // encoder fed by upstream Ambisonics that goes silent between cues.
    if (data.inputs[0].silenceFlags == getChannelMask(data.inputs[0].numChannels)) {
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        for (int32 i = 0; i < numCh; ++i)
            if (in[i] != out[i]) memset(out[i], 0, bytes);
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;

    if (numCh < 4) {  // need full first-order; otherwise pass through
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
void X2UHJProcessor::processBlock(S** in, S** out, int32 n) {
    for (int32 i = 0; i < n; ++i) {
        const double acn[4] = { (double)in[0][i], (double)in[1][i],
                                (double)in[2][i], (double)in[3][i] };
        double L, R, T, Q;
        encoder.process(acn, L, R, T, Q);
        out[0][i] = (S)L; out[1][i] = (S)R; out[2][i] = (S)T; out[3][i] = (S)Q;
    }
}

tresult PLUGIN_API X2UHJProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API X2UHJProcessor::setState(IBStream*)  { return kResultOk; }
tresult PLUGIN_API X2UHJProcessor::getState(IBStream*)  { return kResultOk; }

tresult PLUGIN_API X2UHJProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0]) == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

// Branded editor (stateless: no controls, just title + logo) — matches the
// rest of the suite, which always ships a finished GUI.
IPlugView* PLUGIN_API X2UHJProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "x2uhj.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API X2UHJProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription*, VSTGUI::VST3Editor*) {
    if (name && std::string(name) == "QuadratureReadout")
        return new Seam::QuadratureReadoutView(VSTGUI::CRect(0, 0, 240, 96), &encoder);
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::X2UHJProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM X2UHJ",
        0,
        "Fx|Tools",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::X2UHJProcessor::createInstance)
END_FACTORY
