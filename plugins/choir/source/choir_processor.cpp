//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "choir_processor.h"
#include "choir_ids.h"
#include "choir_state.h"
#include "choir_views.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(choir::Param p) { return kParamPower + (ParamID)p; }

tresult PLUGIN_API ChoirProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four channels: channel c listens to input c (the TETRAREC A, patch
    // inputs 5-8) and sings on output c.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
    auto* out = new RangeParameter(STR16("Output"), kParamOutput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    out->setPrecision(3);
    parameters.addParameter(out);
    return kResultOk;
}

tresult PLUGIN_API ChoirProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

// Every band is designed here, never in process().
tresult PLUGIN_API ChoirProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        choir::applyTo(box_.plain(), engine_);   // recalled values become the targets...
        engine_.reset();                         // ...and the ramps start on them
    } else {
        engine_.release();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API ChoirProcessor::process(ProcessData& data) {
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
                box_.store((choir::Param)(id - kParamPower), v);
        }
    }
    choir::applyTo(box_.plain(), engine_);

    if (data.numOutputs > 0 && data.numSamples > 0) {
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= choir::kChannels &&
            data.outputs[0].numChannels >= choir::kChannels;
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

tresult PLUGIN_API ChoirProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ChoirProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    choir::readState(state, box_);
    for (int i = 0; i < choir::kNumParams; ++i)
        setParamNormalized(idOf((choir::Param)i), box_.normalized((choir::Param)i));
    return kResultOk;
}

tresult PLUGIN_API ChoirProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    choir::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API ChoirProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API ChoirProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "choir.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API ChoirProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (!name) return nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor, structure = VSTGUI::kGreyCColor,
                   bg = VSTGUI::kBlackCColor, track = VSTGUI::kGreyCColor,
                   azure(0x4a, 0x9e, 0xc8, 0xff), meter(0xc8, 0xa2, 0x4a, 0xff);
    if (description) {
        description->getColor("TextLight", text);
        description->getColor("Structure", structure);
        description->getColor("BgDark", bg);
        description->getColor("SliderTrack", track);
        description->getColor("SliderActive", azure);     // the RESET square, as POWER's checkmark
        description->getColor("MeterFill", meter);        // the grid's bars are meters
    }
    if (std::string(name) == "ChoirReset")
        return new ChoirResetButton(VSTGUI::CRect(0, 0, 14, 14), &engine_, structure, bg, azure);
    if (std::string(name) == "ChoirGrid") {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        if (!font) font = VSTGUI::kNormalFontSmall;
        return new ChoirGrid(VSTGUI::CRect(0, 0, 400, 176), &engine_, font, text, track, meter, structure);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::ChoirProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM CHOIR",
        0,
        "Fx",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::ChoirProcessor::createInstance)
END_FACTORY
