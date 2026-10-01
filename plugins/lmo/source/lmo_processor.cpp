//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "lmo_processor.h"
#include "lmo_ids.h"
#include "version.h"
#include "seam_state.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cstring>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static const ParamID kStateIds[5] = {
    kParamPower, kParamFrequency, kParamGlide, kParamDelta, kParamVolume
};

tresult PLUGIN_API LmoProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // A generator still declares an input: with zero input buses a host
    // routes the track AROUND the insert (reference: multipink). The input
    // is never read; process() writes every output channel.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    auto* f = new RangeParameter(STR16("f"), kParamFrequency, STR16("Hz"),
        kLmoFMin, kLmoFMax, kLmoFDefault, 0, ParameterInfo::kCanAutomate);
    f->setPrecision(2);
    parameters.addParameter(f);

    auto* glide = new RangeParameter(STR16("Glide"), kParamGlide, STR16("s"),
        0.0, kLmoGlideMax, 0.0, 0, ParameterInfo::kCanAutomate);
    glide->setPrecision(2);
    parameters.addParameter(glide);

    auto* delta = new RangeParameter(STR16("Delta"), kParamDelta, STR16("Hz"),
        0.0, kLmoDeltaMax, 0.0, 0, ParameterInfo::kCanAutomate);
    delta->setPrecision(2);
    parameters.addParameter(delta);

    auto* vol = new RangeParameter(STR16("Volume"), kParamVolume, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    vol->setPrecision(3);
    parameters.addParameter(vol);

    auto* fnow = new RangeParameter(STR16("f now"), kParamFNow, STR16("Hz"),
        kLmoFMin, kLmoFMax, kLmoFDefault, 0, ParameterInfo::kIsReadOnly);
    fnow->setPrecision(2);
    parameters.addParameter(fnow);

    return kResultOk;
}

tresult PLUGIN_API LmoProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API LmoProcessor::setupProcessing(ProcessSetup& setup) {
    tresult res = SingleComponentEffect::setupProcessing(setup);
    engine_.prepare(sampleRate());
    return res;
}

tresult PLUGIN_API LmoProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        applyParams();      // recalled state becomes the targets...
        engine_.reset();    // ...and the engine starts ON them, no glide from defaults
    }
    return SingleComponentEffect::setActive(state);
}

void LmoProcessor::applyParams() {
    auto plain = [&](ParamID id) -> double {
        auto* p = parameters.getParameter(id);
        return p ? p->toPlain(p->getNormalized()) : 0.0;
    };
    engine_.setGlide(plain(kParamGlide));
    engine_.setFrequency(plain(kParamFrequency));
    engine_.setDelta(plain(kParamDelta));
    engine_.setVolume(plain(kParamVolume));
    engine_.setPower(plain(kParamPower) >= 0.5);
}

tresult PLUGIN_API LmoProcessor::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        const int32 nq = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < nq; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            const int32 np = q->getPointCount();
            if (np <= 0) continue;
            int32 off; ParamValue v;
            if (q->getPoint(np - 1, off, v) == kResultOk)
                setParamNormalized(q->getParameterId(), v);
        }
    }
    applyParams();   // the engine ignores unchanged targets

    if (data.numOutputs > 0 && data.numSamples > 0) {
        // A generator is never silent by inheritance (reference: multipink).
        data.outputs[0].silenceFlags = 0;
        const int32 outCh = data.outputs[0].numChannels;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        if (outCh < lmo::kChannels) {
            const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
            for (int32 c = 0; c < outCh; ++c) if (out[c]) memset(out[c], 0, bytes);
        } else if (data.symbolicSampleSize == kSample32) {
            engine_.process(reinterpret_cast<float* const*>(out), data.numSamples);
        } else {
            engine_.process(reinterpret_cast<double* const*>(out), data.numSamples);
        }
    }

    if (auto* oc = data.outputParameterChanges) {
        int32 idx;
        if (auto* q = oc->addParameterData(kParamFNow, idx)) {
            const double f = std::min(kLmoFMax, std::max(kLmoFMin, engine_.currentFrequency()));
            int32 off = 0;
            q->addPoint(0, (f - kLmoFMin) / (kLmoFMax - kLmoFMin), off);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API LmoProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// State: five normalized doubles, append-only (seam_state.h): a short blob
// keeps the registered defaults for the fields it lacks.
tresult PLUGIN_API LmoProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    double saved[5];
    for (int i = 0; i < 5; ++i) {
        auto* p = parameters.getParameter(kStateIds[i]);
        saved[i] = p ? p->getInfo().defaultNormalizedValue : 0.0;
    }
    Seam::readStateDoubles(state, saved, 5);
    for (int i = 0; i < 5; ++i) setParamNormalized(kStateIds[i], saved[i]);
    return kResultOk;   // the engine picks the values up in the next process()
}

tresult PLUGIN_API LmoProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    for (ParamID id : kStateIds) {
        auto* p = parameters.getParameter(id);
        s.writeDouble(p ? p->getNormalized() : 0.0);
    }
    return kResultOk;
}

tresult PLUGIN_API LmoProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API LmoProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "lmo.uidesc");
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::LmoProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM LMO",
        0,
        "Fx|Generator",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::LmoProcessor::createInstance)
END_FACTORY
