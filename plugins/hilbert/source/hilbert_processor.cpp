//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hilbert — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "hilbert_processor.h"
#include "hilbert_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "hilbert_readout_view.h"
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

HilbertProcessor::HilbertProcessor() {}

tresult PLUGIN_API HilbertProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput (STR16("Mono In"),  SpeakerArr::kMono);
    addAudioOutput(STR16("I/Q Out"),  SpeakerArr::kStereo);

    // One parameter: the active topology (RBJ / Polyphase), as a 2-entry list.
    auto* topo = new StringListParameter(STR16("Topology"), kTopologyId);
    topo->appendString(STR16("RBJ"));
    topo->appendString(STR16("Polyphase"));
    parameters.addParameter(topo);
    return kResultOk;
}

tresult PLUGIN_API HilbertProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API HilbertProcessor::setActive(TBool state) {
    if (state) {
        dsp.prepare(processSetup.sampleRate);
        applyTopology(getParamNormalized(kTopologyId)); // honour recalled state
        dsp.reset();
    }
    return SingleComponentEffect::setActive(state);
}

void HilbertProcessor::applyTopology(double normalized) {
    dsp.setTopology(normalized >= 0.5 ? hilbert::Topology::Polyphase
                                      : hilbert::Topology::RBJ);
}

tresult PLUGIN_API HilbertProcessor::process(ProcessData& data) {
    // Apply a topology change from this block's parameter automation.
    if (data.inputParameterChanges) {
        int32 nq = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < nq; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (q && q->getParameterId() == kTopologyId) {
                int32 n = q->getPointCount();
                if (n > 0) {
                    int32 off; ParamValue v;
                    if (q->getPoint(n - 1, off, v) == kResultOk) {
                        setParamNormalized(kTopologyId, v);
                        applyTopology(v);
                    }
                }
            }
        }
    }

    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    int32 numOut = data.outputs[0].numChannels;
    uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    // Silent input → silent output. As in the suite's other IIR plugins, the
    // all-pass tail from prior content is suppressed rather than decayed —
    // acceptable for a teaching object fed by sources that go silent between cues.
    if (data.inputs[0].silenceFlags != 0) {
        data.outputs[0].silenceFlags = (numOut == 2) ? 0x3 : 0x1;
        for (int32 i = 0; i < numOut; ++i)
            if (out[i]) memset(out[i], 0, bytes);
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;

    if (numOut < 2) {  // need a stereo (I/Q) output bus; otherwise pass through
        if (in[0] && out[0] && in[0] != out[0]) memcpy(out[0], in[0], bytes);
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
void HilbertProcessor::processBlock(S** in, S** out, int32 n) {
    for (int32 i = 0; i < n; ++i) {
        double inPhase, quad;
        dsp.process((double)in[0][i], inPhase, quad);
        out[0][i] = (S)inPhase; out[1][i] = (S)quad;
    }
}

tresult PLUGIN_API HilbertProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API HilbertProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    float topo = 0.0f;
    if (!s.readFloat(topo)) return kResultFalse;
    setParamNormalized(kTopologyId, topo);
    applyTopology(topo);
    return kResultOk;
}

tresult PLUGIN_API HilbertProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    s.writeFloat((float)getParamNormalized(kTopologyId));
    return kResultOk;
}

tresult PLUGIN_API HilbertProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0]) == 1 &&
        SpeakerArr::getChannelCount(out[0]) == 2)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API HilbertProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "hilbert.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API HilbertProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (name && std::string(name) == "HilbertReadout") {
        // Match the suite's text scheme: all text is TextLight, in the same
        // monospace font as the title block, resolved from the uidesc.
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        VSTGUI::CColor label = VSTGUI::kWhiteCColor, value = VSTGUI::kWhiteCColor;
        if (description) {
            description->getColor("TextLight", label);
            description->getColor("TextLight", value);
        }
        return new Seam::HilbertReadoutView(
            VSTGUI::CRect(0, 0, 240, 120), &dsp, font, label, value);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::HilbertProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM hilbert",
        0,
        "Fx|Tools",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::HilbertProcessor::createInstance)
END_FACTORY
