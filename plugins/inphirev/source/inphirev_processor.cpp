//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "inphirev_processor.h"
#include "inphirev_ids.h"
#include "inphirev_state.h"
#include "inphirev_views.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(inphirev::Param p) { return kParamPower + (ParamID)p; }

// The distance is stored as the grid index j (0..14) and shown in metres
// on the grid of the current STONE id, read from the StoneId parameter:
// the controller's value, which the window shows, not the audio thread's.
// Typing a distance picks the nearest grid point of that id.
class DistanceParameter : public RangeParameter {
public:
    explicit DistanceParameter(const Parameter* stoneId)
        : RangeParameter(STR16("Distance"), kParamGrid, STR16(""),
                         0.0, (ParamValue)inphirev::kGridMax, (ParamValue)inphirev::kDefaultGrid,
                         inphirev::kGridSteps, ParameterInfo::kCanAutomate),
          stoneId_(stoneId) {}

    void toString(ParamValue norm, String128 string) const SMTG_OVERRIDE {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3f m",
                      inphirev::gridMetres(currentId(), inphirev::normalizedToGrid(norm)));
        UString(string, 128).fromAscii(buf);
    }

    bool fromString(const TChar* string, ParamValue& norm) const SMTG_OVERRIDE {
        char buf[64];
        UString(const_cast<TChar*>(string), tstrlen(string) + 1).toAscii(buf, sizeof buf);
        char* end = nullptr;
        const double metres = std::strtod(buf, &end);
        if (end == buf || !std::isfinite(metres)) return false;
        const int id = currentId();
        int best = 0;
        for (int j = 1; j <= inphirev::kGridMax; ++j)
            if (std::fabs(inphirev::gridMetres(id, j) - metres) <
                std::fabs(inphirev::gridMetres(id, best) - metres)) best = j;
        norm = inphirev::gridToNormalized(best);
        return true;
    }

private:
    int currentId() const {
        return stoneId_ ? inphirev::normalizedToStoneId(stoneId_->getNormalized()) : 0;
    }
    const Parameter* stoneId_;
};

tresult PLUGIN_API InphirevProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four faces of STONE, A-format, face k on channel k (stunedrev's bus).
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    using namespace inphirev;
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    // The suite's StoneId (multipink, ltglide): "?" = undeclared, phase 0.
    auto* stone = new StringListParameter(
        STR16("STONE"), kParamStoneId, STR16(""),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    stone->appendString(STR16("?"));
    stone->appendString(STR16("1"));
    stone->appendString(STR16("2"));
    stone->appendString(STR16("3"));
    stone->appendString(STR16("4"));
    stone->appendString(STR16("5"));
    stone->appendString(STR16("6"));
    stone->appendString(STR16("7"));
    stone->appendString(STR16("8"));
    parameters.addParameter(stone);

    parameters.addParameter(new DistanceParameter(stone));

    auto* sections = new RangeParameter(STR16("Sections"), kParamSections, STR16(""),
        (ParamValue)kSectionsMin, (ParamValue)kMaxSections, (ParamValue)kDefaultSections,
        kSectionsSteps, ParameterInfo::kCanAutomate);
    sections->setPrecision(0);
    parameters.addParameter(sections);

    return kResultOk;
}

tresult PLUGIN_API InphirevProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

void InphirevProcessor::applyBox() {
    const inphirev::Plain p = box_.plain();
    engine_.setSettings(p.stoneId, p.grid, p.sections);
    engine_.setPower(p.power);
}

// The arena is allocated and zeroed here, never in process(): about 290 MiB
// at 48 kHz, 579 MiB at 96 kHz, 1.13 GiB at 192 kHz. The recalled settings
// go in before prepare, while the engine holds no arena, so they are taken
// at once and the first table is theirs: no settle, no reset on activation.
// A failed allocation leaves the plugin silent and the footer says so.
tresult PLUGIN_API InphirevProcessor::setActive(TBool state) {
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

tresult PLUGIN_API InphirevProcessor::process(ProcessData& data) {
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
            if (id < kParamPower || id > kParamSections) continue;
            if (q->getPoint(np - 1, off, v) == kResultOk)
                box_.store((inphirev::Param)(id - kParamPower), v);
        }
    }
    applyBox();

    if (data.numOutputs > 0 && data.numSamples > 0) {
        // A memory of minutes is never silent by inheritance.
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= inphirev::kFaces &&
            data.outputs[0].numChannels >= inphirev::kFaces;
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

tresult PLUGIN_API InphirevProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// setState runs on the UI thread while process() may run: the values go to
// the box (no order matters), and the editor follows.
tresult PLUGIN_API InphirevProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    inphirev::readState(state, box_);
    for (int i = 0; i < inphirev::kNumParams; ++i)
        setParamNormalized(idOf((inphirev::Param)i), box_.normalized((inphirev::Param)i));
    return kResultOk;
}

tresult PLUGIN_API InphirevProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    inphirev::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API InphirevProcessor::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult r = SingleComponentEffect::setParamNormalized(tag, value);
    // The distance menu lists the metres of the current id's grid: a new id
    // keeps j and rebuilds the entries (the editor listens to changed()).
    if (r == kResultOk && tag == kParamStoneId)
        if (Parameter* grid = parameters.getParameter(kParamGrid)) grid->changed();
    return r;
}

tresult PLUGIN_API InphirevProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == inphirev::kFaces &&
        SpeakerArr::getChannelCount(out[0]) == inphirev::kFaces)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API InphirevProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "inphirev.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API InphirevProcessor::createCustomView(
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
    if (std::string(name) == "InphirevReset")
        return new InphirevResetButton(VSTGUI::CRect(0, 0, 14, 14), &engine_, frame, idle, azure);
    if (std::string(name) == "InphirevFooter") {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        if (!font) font = VSTGUI::kNormalFontSmall;
        return new InphirevFooter(VSTGUI::CRect(0, 0, 400, 70), &engine_, font, text);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::InphirevProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM INPHIREV",
        0,
        "Fx|Reverb",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::InphirevProcessor::createInstance)
END_FACTORY
