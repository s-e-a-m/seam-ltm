//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "bastooge_processor.h"
#include "bastooge_ids.h"
#include "seam_denormals.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

// The cut slider: equal travel, equal ratio (bastooge_state.h's mapping, so
// the DSP and the host's display agree).
class CutParameter : public RangeParameter {
public:
    CutParameter()
        : RangeParameter(STR16("Cut"), kCut, STR16("Hz"),
                         bastooge::kCutMin, bastooge::kCutMax, bastooge::kCutDefault,
                         0, ParameterInfo::kCanAutomate) {
        setPrecision(0);
        setNormalized(toNormalized(bastooge::kCutDefault));
        info.defaultNormalizedValue = getNormalized();
    }
    ParamValue toPlain(ParamValue n) const SMTG_OVERRIDE { return bastooge::cutFromNormalized(n); }
    ParamValue toNormalized(ParamValue v) const SMTG_OVERRIDE { return bastooge::cutToNormalized(v); }
};

BASTOOGEProcessor::BASTOOGEProcessor() {}

tresult PLUGIN_API BASTOOGEProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // 4 in (AmbiX a0..a3) -> 4 out (LFU, RFD, RBU, LBD). VST3 has no
    // tetrahedral arrangement, so the output is declared as 4-channel
    // first-order and the host routes it to the four faces, as in bamodulex.
    addAudioInput (STR16("AmbiX In"),      SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("S.T.OOGE Out"),  SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter(new CutParameter);

    auto* test = new StringListParameter(STR16("Test"), kTest);
    test->appendString(STR16("Normal"));
    test->appendString(STR16("W"));
    test->appendString(STR16("X"));
    test->appendString(STR16("Y"));
    test->appendString(STR16("Z"));
    parameters.addParameter(test);

    // A plain toggle, not kIsBypass: a host's bypass passes the input
    // through, and this one keeps the matrix.
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0.0,
                            ParameterInfo::kCanAutomate, kBypass);
    return kResultOk;
}

tresult PLUGIN_API BASTOOGEProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

void BASTOOGEProcessor::applyParams() {
    engine_.setCut(box_.cut());
    engine_.setTest(box_.test());     // the box holds 0..4 only (bastooge_state.h)
    engine_.setBypass(box_.bypass());
}

tresult PLUGIN_API BASTOOGEProcessor::setActive(TBool state) {
    if (state) {
        applyParams();                       // the recalled values become the targets...
        engine_.prepare(processSetup.sampleRate);   // ...and prepare snaps to them, no glide
        readout_.publish(engine_, box_.test(), box_.bypass());
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API BASTOOGEProcessor::process(ProcessData& data) {
    if (auto* changes = data.inputParameterChanges) {
        for (int32 i = 0; i < changes->getParameterCount(); ++i) {
            IParamValueQueue* q = changes->getParameterData(i);
            if (!q) continue;
            // The last point of the block, applied at its start (suite convention).
            int32 off; ParamValue v;
            const int32 np = q->getPointCount();
            if (np <= 0 || q->getPoint(np - 1, off, v) != kResultOk) continue;
            switch (q->getParameterId()) {
                case kCut:    box_.storeCut(bastooge::cutFromNormalized(v)); break;
                case kTest:   box_.storeTest(bastooge::testFromNormalized(v)); break;
                case kBypass: box_.storeBypass(v >= 0.5); break;
                default: break;
            }
        }
    }
    applyParams();

    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    const int32 inCh  = data.inputs[0].numChannels;
    const int32 outCh = data.outputs[0].numChannels;
    const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    // Silent input -> silent output. As in x2uhj, the filter tails from prior
    // content are suppressed rather than allowed to decay.
    if (data.inputs[0].silenceFlags == getChannelMask(inCh)) {
        data.outputs[0].silenceFlags = getChannelMask(outCh);
        for (int32 c = 0; c < outCh; ++c) memset(out[c], 0, bytes);
        readout_.publish(engine_, box_.test(), box_.bypass());
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;

    if (inCh < 4 || outCh < 4) {
        for (int32 c = 0; c < outCh; ++c) memset(out[c], 0, bytes);
        return kResultOk;
    }

    {
        // The W all-pass and the dipole high-passes decay in silence.
        ScopedNoDenormals noDenormals;
        // In place is safe: each sample's four inputs are read before its outputs are written.
        if (data.symbolicSampleSize == kSample32)
            engine_.process<Sample32>(reinterpret_cast<const Sample32* const*>(in),
                                      reinterpret_cast<Sample32* const*>(out), data.numSamples);
        else
            engine_.process<Sample64>(reinterpret_cast<const Sample64* const*>(in),
                                      reinterpret_cast<Sample64* const*>(out), data.numSamples);
    }
    readout_.publish(engine_, box_.test(), box_.bypass());
    return kResultOk;
}

tresult PLUGIN_API BASTOOGEProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API BASTOOGEProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    bastooge::readState(state, box_);
    setParamNormalized(kCut,    bastooge::cutToNormalized(box_.cut()));
    setParamNormalized(kTest,   bastooge::testToNormalized(box_.test()));
    setParamNormalized(kBypass, box_.bypass() ? 1.0 : 0.0);
    return kResultOk;
}

tresult PLUGIN_API BASTOOGEProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    bastooge::writeState(state, box_);
    return kResultOk;
}

// 4 in, 4 out, nothing else (as bamodulex).
tresult PLUGIN_API BASTOOGEProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0]) == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API BASTOOGEProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "bastooge.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API BASTOOGEProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (name && std::string(name) == "BastoogeReadout") {
        VSTGUI::CFontRef font = description ? description->getFont("ReadoutFont") : nullptr;
        VSTGUI::CColor color = VSTGUI::kWhiteCColor;
        if (description) description->getColor("TextLight", color);
        // The uidesc's origin and size are applied to the view after creation.
        return new bastooge::ReadoutView(VSTGUI::CRect(0, 0, 280, 32), &readout_, font, color);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::BASTOOGEProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM BASTOOGE",
        0,
        "Fx|Spatial",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::BASTOOGEProcessor::createInstance)
END_FACTORY
