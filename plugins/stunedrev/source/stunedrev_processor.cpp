//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "stunedrev_processor.h"
#include "stunedrev_ids.h"
#include "stunedrev_state.h"
#include "stunedrev_views.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(stunedrev::Param p) { return kParamPower + (ParamID)p; }

tresult PLUGIN_API StunedrevProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four lines, one per face of STONED, in the original's order:
    // sqrt(2), phi, e, pi on channels 1-4.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    using namespace stunedrev;
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    const char16* names[kLines] = { STR16("t sqrt2"), STR16("t phi"), STR16("t e"), STR16("t pi") };
    for (int j = 0; j < kLines; ++j) {
        auto* t = new RangeParameter(names[j], kParamT1 + j, STR16("ms"),
            kTMin, kTMax, kDefaultTimes[j], kTimeSteps, ParameterInfo::kCanAutomate);
        t->setPrecision(0);
        parameters.addParameter(t);
    }

    auto* in = new RangeParameter(STR16("Input"), kParamInput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    in->setPrecision(3);
    parameters.addParameter(in);

    auto* out = new RangeParameter(STR16("Output"), kParamOutput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    out->setPrecision(3);
    parameters.addParameter(out);

    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

// The arena is allocated and zeroed here, never in process(): 588 MiB at
// 96 kHz, 1.2 GiB at 192 kHz. A failed allocation leaves the plugin silent
// and the footer says so; the host keeps running.
tresult PLUGIN_API StunedrevProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        stunedrev::applyTo(box_.plain(), engine_);   // recalled values become the targets...
        engine_.reset();                             // ...and the ramps start on them
    } else {
        engine_.release();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API StunedrevProcessor::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        const int32 nq = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < nq; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            const int32 np = q->getPointCount();
            if (np <= 0) continue;
            // The last point of the block, applied at its start (suite convention).
            int32 off; ParamValue v;
            const ParamID id = q->getParameterId();
            if (id < kParamPower || id > kParamOutput) continue;
            if (q->getPoint(np - 1, off, v) == kResultOk)
                box_.store((stunedrev::Param)(id - kParamPower), v);
        }
    }
    stunedrev::applyTo(box_.plain(), engine_);

    if (data.numOutputs > 0 && data.numSamples > 0) {
        // A memory of minutes is never silent by inheritance.
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= stunedrev::kLines &&
            data.outputs[0].numChannels >= stunedrev::kLines;
        if (!shaped) {
            const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
            for (int32 c = 0; c < data.outputs[0].numChannels; ++c) if (out[c]) memset(out[c], 0, bytes);
        } else {
            void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
            if (data.symbolicSampleSize == kSample32)
                engine_.process(reinterpret_cast<float**>(in), reinterpret_cast<float**>(out), data.numSamples);
            else
                engine_.process(reinterpret_cast<double**>(in), reinterpret_cast<double**>(out), data.numSamples);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// setState runs on the UI thread while process() may run: the values go to
// the box (no order matters), and the editor follows.
tresult PLUGIN_API StunedrevProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    stunedrev::readState(state, box_);
    for (int i = 0; i < stunedrev::kNumParams; ++i)
        setParamNormalized(idOf((stunedrev::Param)i), box_.normalized((stunedrev::Param)i));
    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    stunedrev::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API StunedrevProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "stunedrev.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API StunedrevProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (!name) return nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor, frame = VSTGUI::kGreyCColor,
                   idle = VSTGUI::kBlackCColor, azure(0x4a, 0x9e, 0xc8, 0xff);
    if (description) {
        description->getColor("TextLight", text);
        description->getColor("Structure", frame);
        description->getColor("BgDark", idle);
        description->getColor("SliderActive", azure);
    }
    if (std::string(name) == "StunedrevReset")
        return new StunedrevResetButton(VSTGUI::CRect(0, 0, 14, 14), &engine_, frame, idle, azure);
    if (std::string(name) == "StunedrevFooter") {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        if (!font) font = VSTGUI::kNormalFontSmall;
        return new StunedrevFooter(VSTGUI::CRect(0, 0, 400, 36), &engine_, font, text);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::StunedrevProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM STUNEDREV",
        0,
        "Fx|Reverb",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::StunedrevProcessor::createInstance)
END_FACTORY
