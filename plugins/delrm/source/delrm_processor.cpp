//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "delrm_processor.h"
#include "delrm_ids.h"
#include "delrm_state.h"
#include "delrm_views.h"
#include "seam_meter.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(delrm::Param p) { return kParamPower + (ParamID)p; }

// A gain reduction shown as a negative dB value, while its normalized value
// grows with the depth: the bar, drawn right to left, grows as the
// compressor works.
class ReductionParameter : public RangeParameter {
public:
    ReductionParameter(const TChar* title, ParamID id)
        : RangeParameter(title, id, STR16("dB"), 0.0, delrm::kGrRangeDb, 0.0, 0, ParameterInfo::kIsReadOnly) {}
    void toString(ParamValue norm, String128 string) const SMTG_OVERRIDE {
        const double db = toPlain(norm);
        char s[32];
        std::snprintf(s, sizeof s, db >= 0.05 ? "-%.1f" : "0.0", db);
        UString(string, 128).fromAscii(s);
    }
};

tresult PLUGIN_API DelrmProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four channels in the original's order (dac~ 9 10 11 12): comb, triple
    // product, comb, triple product.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    using namespace delrm;
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    auto* dist = new RangeParameter(STR16("Distance"), kParamDistance, STR16("m"),
        0.0, kMaxMetres, kDefaultMetres, 0, ParameterInfo::kCanAutomate);
    dist->setPrecision(3);
    parameters.addParameter(dist);

    auto* out = new RangeParameter(STR16("Output"), kParamOutput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    out->setPrecision(3);
    parameters.addParameter(out);

    const char16* inNames[kChannels] = { STR16("in 1"), STR16("in 2"), STR16("in 3"), STR16("in 4") };
    for (int c = 0; c < kChannels; ++c) {
        auto* m = new RangeParameter(inNames[c], kParamIn1 + c, STR16("dB"),
            kInFloorDb, kInTopDb, kInFloorDb, 0, ParameterInfo::kIsReadOnly);
        m->setPrecision(1);
        parameters.addParameter(m);
    }
    parameters.addParameter(new ReductionParameter(STR16("GR 2"), kParamGr2));
    parameters.addParameter(new ReductionParameter(STR16("GR 4"), kParamGr4));
    return kResultOk;
}

tresult PLUGIN_API DelrmProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

// The delay lines are allocated and zeroed here, never in process().
tresult PLUGIN_API DelrmProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        delrm::applyTo(box_.plain(), engine_);   // recalled values become the targets...
        engine_.reset();                         // ...and the ramps start on them
    } else {
        engine_.release();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API DelrmProcessor::process(ProcessData& data) {
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
                box_.store((delrm::Param)(id - kParamPower), v);
        }
    }
    delrm::applyTo(box_.plain(), engine_);

    if (data.numOutputs > 0 && data.numSamples > 0) {
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= delrm::kChannels &&
            data.outputs[0].numChannels >= delrm::kChannels;
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
    publishMeters(data);
    return kResultOk;
}

// Read-only parameters, one point per block (the dslar idiom).
void DelrmProcessor::publishMeters(ProcessData& data) {
    auto* oc = data.outputParameterChanges;
    if (!oc) return;
    auto put = [oc](ParamID id, double norm) {
        int32 idx = 0;
        if (auto* q = oc->addParameterData(id, idx)) { int32 o = 0; q->addPoint(0, norm, o); }
    };
    using namespace delrm;
    const double span = kInTopDb - kInFloorDb;
    for (int c = 0; c < kChannels; ++c) {
        const double db = seam::meter::lin2db(engine_.inputPeak(c), kInFloorDb);
        put(kParamIn1 + c, std::min(1.0, std::max(0.0, (db - kInFloorDb) / span)));
    }
    put(kParamGr2, std::min(1.0, engine_.reductionDb(0) / kGrRangeDb));
    put(kParamGr4, std::min(1.0, engine_.reductionDb(1) / kGrRangeDb));
}

tresult PLUGIN_API DelrmProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API DelrmProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    delrm::readState(state, box_);
    for (int i = 0; i < delrm::kNumParams; ++i)
        setParamNormalized(idOf((delrm::Param)i), box_.normalized((delrm::Param)i));
    return kResultOk;
}

tresult PLUGIN_API DelrmProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    delrm::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API DelrmProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API DelrmProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "delrm.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API DelrmProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (!name || std::string(name) != "DelrmFooter") return nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor;
    if (description) description->getColor("TextLight", text);
    VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
    if (!font) font = VSTGUI::kNormalFontSmall;
    return new DelrmFooter(VSTGUI::CRect(0, 0, 260, 16), &engine_, font, text);
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::DelrmProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM DELRM",
        0,
        "Fx|Delay",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::DelrmProcessor::createInstance)
END_FACTORY
