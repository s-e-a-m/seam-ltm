//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "alogos_processor.h"
#include "alogos_ids.h"
#include "alogos_state.h"
#include "alogos_text.h"
#include "alogos_views.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(alogos::Param p) { return kParamPower + (ParamID)p; }

static bool parseNumber(const TChar* string, double& out) {
    char buf[64];
    UString(const_cast<TChar*>(string), tstrlen(string) + 1).toAscii(buf, sizeof buf);
    char* end = nullptr;
    out = std::strtod(buf, &end);
    return end != buf && std::isfinite(out);
}

// The distance is stored in metres (1..6, continuous) and shown as the grid
// point the engine takes: the snap of the current id, scale and density.
// Typing a distance stores it as typed; the text shows where it lands.
class DistanceParameter : public RangeParameter {
public:
    explicit DistanceParameter(const SettingsView* view)
        : RangeParameter(STR16("Distance"), kParamDistance, STR16(""),   // the text carries "m"
                         alogos::kMinMetres, alogos::kMaxMetres, alogos::kDefaultMetres,
                         0, ParameterInfo::kCanAutomate),
          view_(view) {}

    void toString(ParamValue norm, String128 string) const SMTG_OVERRIDE {
        char buf[32];
        alogos::distanceText(buf, sizeof buf, view_->sc(), view_->dn(), view_->id(),
                             alogos::normalizedToDistance(norm));
        UString(string, 128).fromAscii(buf);
    }
    bool fromString(const TChar* string, ParamValue& norm) const SMTG_OVERRIDE {
        double m;
        if (!parseNumber(string, m)) return false;
        norm = alogos::distanceToNormalized(std::clamp(m, alogos::kMinMetres, alogos::kMaxMetres));
        return true;
    }

private:
    const SettingsView* view_;
};

// Sections 1..128 as asked; the text is the effective count, with
// "(max N)" when N_max at the snapped distance limits it.
class SectionsParameter : public RangeParameter {
public:
    explicit SectionsParameter(const SettingsView* view)
        : RangeParameter(STR16("Sections"), kParamSections, STR16(""),
                         (ParamValue)alogos::kSectionsMin, (ParamValue)alogos::kMaxSections,
                         (ParamValue)alogos::kDefaultSections, alogos::kSectionsSteps,
                         ParameterInfo::kCanAutomate),
          view_(view) {}

    void toString(ParamValue norm, String128 string) const SMTG_OVERRIDE {
        char buf[32];
        alogos::sectionsText(buf, sizeof buf, alogos::normalizedToSections(norm),
                             view_->sc(), view_->dn(), view_->id(), view_->metres());
        UString(string, 128).fromAscii(buf);
    }
    bool fromString(const TChar* string, ParamValue& norm) const SMTG_OVERRIDE {
        double n;
        if (!parseNumber(string, n)) return false;
        norm = alogos::sectionsToNormalized(
            std::clamp((int)std::lround(n), alogos::kSectionsMin, alogos::kMaxSections));
        return true;
    }

private:
    const SettingsView* view_;
};

static StringListParameter* menu(const TChar* title, ParamID id,
                                 std::initializer_list<const TChar*> entries) {
    auto* p = new StringListParameter(title, id, STR16(""),
                                      ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    for (const TChar* e : entries) p->appendString(e);
    return p;
}

tresult PLUGIN_API AlogosProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four faces of STONE, A-format, face k on channel k (stunedrev's bus).
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    // The suite's StoneId (multipink, ltglide): "?" = undeclared, phase 0.
    auto* stone = menu(STR16("STONE"), kParamStoneId,
        { STR16("?"), STR16("1"), STR16("2"), STR16("3"), STR16("4"),
          STR16("5"), STR16("6"), STR16("7"), STR16("8") });
    parameters.addParameter(stone);

    auto* distance = new DistanceParameter(&view_);
    parameters.addParameter(distance);
    auto* sections = new SectionsParameter(&view_);
    sections->setPrecision(0);
    parameters.addParameter(sections);

    auto* scale = menu(STR16("Scale"), kParamScale,
        { STR16("φ"), STR16("√" "2"), STR16("e"), STR16("π") });
    parameters.addParameter(scale);
    auto* density = menu(STR16("Density"), kParamDensity, { STR16("1"), STR16("2"), STR16("4") });
    parameters.addParameter(density);
    parameters.addParameter(menu(STR16("Gain"), kParamGain,
        { STR16("1/φ"), STR16("1/√" "2"), STR16("1/e"), STR16("1/π"),
          STR16("√(√" "2−" "1)") }));

    view_.stoneId = stone; view_.distance = distance; view_.scale = scale; view_.density = density;
    return kResultOk;
}

tresult PLUGIN_API AlogosProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

void AlogosProcessor::applyBox() {
    const alogos::Plain p = box_.plain();
    engine_.setSettings(p.stoneId, p.metres, p.sections, p.scale, p.density, p.gain);
    engine_.setPower(p.power);
}

// The arena is allocated and zeroed here, sized for the setting held, never
// in process(). The box's values go in before prepare, while the engine holds
// no sieve, so they are taken at once and the first table is theirs: no
// settle, no reset on activation. This also replaces the engine's own pending
// default (STONE 1) with the box's (STONE "?", or the recalled id).
// A failed allocation leaves the plugin silent and the footer says so.
tresult PLUGIN_API AlogosProcessor::setActive(TBool state) {
    if (state) {
        engine_.release();
        applyBox();
        engine_.prepare(sampleRate());
        engine_.reset();   // the POWER ramp starts on its target
    } else {
        engine_.release();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API AlogosProcessor::process(ProcessData& data) {
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
            if (id < kParamPower || id > kParamGain) continue;
            if (q->getPoint(np - 1, off, v) == kResultOk)
                box_.store((alogos::Param)(id - kParamPower), v);
        }
    }
    applyBox();

    if (data.numOutputs > 0 && data.numSamples > 0) {
        // A memory of minutes is never silent by inheritance.
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= alogos::kFaces &&
            data.outputs[0].numChannels >= alogos::kFaces;
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

tresult PLUGIN_API AlogosProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// setState runs on the UI thread while process() may run: the values go to
// the box (no order matters), and the editor follows.
tresult PLUGIN_API AlogosProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    alogos::readState(state, box_);
    for (int i = 0; i < alogos::kNumParams; ++i)
        setParamNormalized(idOf((alogos::Param)i), box_.normalized((alogos::Param)i));
    return kResultOk;
}

tresult PLUGIN_API AlogosProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    alogos::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API AlogosProcessor::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult r = SingleComponentEffect::setParamNormalized(tag, value);
    if (r != kResultOk) return r;
    // The snap depends on id, scale and density; N_max on those and the
    // distance. The editor listens to changed(); the host's generic views
    // to kParamValuesChanged.
    const bool snap = tag == kParamStoneId || tag == kParamScale || tag == kParamDensity;
    if (snap || tag == kParamDistance) {
        if (snap)
            if (Parameter* p = parameters.getParameter(kParamDistance)) p->changed();
        if (Parameter* p = parameters.getParameter(kParamSections)) p->changed();
        if (componentHandler) componentHandler->restartComponent(kParamValuesChanged);
    }
    return r;
}

tresult PLUGIN_API AlogosProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == alogos::kFaces &&
        SpeakerArr::getChannelCount(out[0]) == alogos::kFaces)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API AlogosProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "alogos.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API AlogosProcessor::createCustomView(
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
    if (std::string(name) == "AlogosReset")
        return new AlogosResetButton(VSTGUI::CRect(0, 0, 14, 14), &engine_, frame, idle, azure);
    if (std::string(name) == "AlogosFooter") {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        if (!font) font = VSTGUI::kNormalFontSmall;
        return new AlogosFooter(VSTGUI::CRect(0, 0, 400, 70), &engine_, font, text);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::AlogosProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM ALOGOS",
        0,
        "Fx|Reverb",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::AlogosProcessor::createInstance)
END_FACTORY
