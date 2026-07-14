#include "dslar_processor.h"
#include "dslar_ids.h"
#include "dslar_reset_button.h"
#include "version.h"
#include "seam_meter.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

DSLARProcessor::DSLARProcessor() {}

static inline double denorm(double v, double lo, double hi) { return lo + v * (hi - lo); }
static inline double norm  (double p, double lo, double hi) { return (p - lo) / (hi - lo); }

tresult PLUGIN_API DSLARProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput (STR16("Input"),  SpeakerArr::kMono);
    addAudioOutput(STR16("Output"), SpeakerArr::kMono);

    auto add = [&](const TChar* name, ParamID id, double lo, double hi, double def,
                   int32 prec, const TChar* unit) {
        auto* p = new RangeParameter(name, id, unit, lo, hi, def, 0,
                                     ParameterInfo::kCanAutomate);
        p->setPrecision(prec);
        parameters.addParameter(p);
    };
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""), 0.0, 1.0, 0.0, 1,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList));
    add(STR16("Drive"),            kParamDrive,     kDriveMin,  kDriveMax,  kDriveDef,  2, STR16(""));
    add(STR16("Target"),           kParamTarget,    kTargetMin, kTargetMax, kTargetDef, 3, STR16(""));
    add(STR16("Steepness"),        kParamSteepness, kSteepMin,  kSteepMax,  kSteepDef,  1, STR16(""));
    add(STR16("Control smoothing"),kParamSmoothing, kSmoothMin, kSmoothMax, kSmoothDef, 0, STR16("ms"));
    add(STR16("Loop delay"),       kParamLoopDelay, kTab1Min,   kTab1Max,   kTab1Def,   1, STR16("ms"));
    add(STR16("Decorrelation"),    kParamDecorr,    kTab2Min,   kTab2Max,   kTab2Def,   1, STR16("ms"));
    add(STR16("Output"),           kParamOutput,    kOutMin,    kOutMax,    kOutDef,    3, STR16(""));

    parameters.addParameter(new RangeParameter(
        STR16("Meter R"), kParamMeterR, STR16(""), 0.0, 1.0, 0.0, 0,
        ParameterInfo::kIsReadOnly));
    parameters.addParameter(new RangeParameter(
        STR16("Meter G"), kParamMeterG, STR16(""), 0.0, 1.0, 0.0, 0,
        ParameterInfo::kIsReadOnly));

    return kResultOk;
}

tresult PLUGIN_API DSLARProcessor::terminate() { return SingleComponentEffect::terminate(); }

void DSLARProcessor::applyParams() {
    larsen_.setPower(paramPower_.load() >= 0.5);
    larsen_.setDrive(paramDrive_.load());
    larsen_.setTarget(paramTarget_.load());
    larsen_.setSteepness(paramSteep_.load());
    larsen_.setSmoothingMs(paramSmooth_.load());
    larsen_.setLoopDelayMs(paramTab1_.load());
    larsen_.setDecorrelationMs(paramTab2_.load());
    larsen_.setOutput(paramOutput_.load());
}

tresult PLUGIN_API DSLARProcessor::setActive(TBool state) {
    if (state) { larsen_.reset(); applyParams(); }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API DSLARProcessor::setupProcessing(ProcessSetup& setup) {
    larsen_.prepare(setup.sampleRate);
    applyParams();
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API DSLARProcessor::process(ProcessData& data) {
    readParameterChanges(data);
    applyParams();

    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples == 0) {
        // Still publish the (idle) meters so the GUI stays live.
        if (auto* oc = data.outputParameterChanges) {
            int32 idx;
            if (auto* q = oc->addParameterData(kParamMeterR, idx)) { int32 o=0; q->addPoint(0, 0.0, o); }
            if (auto* q = oc->addParameterData(kParamMeterG, idx)) { int32 o=0; q->addPoint(0, 0.0, o); }
        }
        return kResultOk;
    }

    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32)
        processBlock<float>((float**)in, (float**)out, data.numSamples);
    else
        processBlock<double>((double**)in, (double**)out, data.numSamples);

    // Publish r/g meters (normalized, floor -60 dBFS).
    if (auto* oc = data.outputParameterChanges) {
        int32 idx;
        const double rN = seam::meter::lin2norm(larsen_.measuredRms(),  kMeterFloorDb);
        const double gN = seam::meter::lin2norm(larsen_.analysisGain(), kMeterFloorDb);
        if (auto* q = oc->addParameterData(kParamMeterR, idx)) { int32 o=0; q->addPoint(0, rN, o); }
        if (auto* q = oc->addParameterData(kParamMeterG, idx)) { int32 o=0; q->addPoint(0, gN, o); }
    }
    return kResultOk;
}

tresult PLUGIN_API DSLARProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API DSLARProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) {
    if (numIns != 1 || numOuts != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(ins[0]) != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(outs[0]) != 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API DSLARProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double vals[8];
    for (int i = 0; i < 8; ++i) if (!s.readDouble(vals[i])) return kResultFalse;
    paramPower_.store(std::clamp(vals[0], 0.0, 1.0));
    paramDrive_.store(std::clamp(vals[1], kDriveMin, kDriveMax));
    paramTarget_.store(std::clamp(vals[2], kTargetMin, kTargetMax));
    paramSteep_.store(std::clamp(vals[3], kSteepMin, kSteepMax));
    paramSmooth_.store(std::clamp(vals[4], kSmoothMin, kSmoothMax));
    paramTab1_.store(std::clamp(vals[5], kTab1Min, kTab1Max));
    paramTab2_.store(std::clamp(vals[6], kTab2Min, kTab2Max));
    paramOutput_.store(std::clamp(vals[7], kOutMin, kOutMax));

    auto setN = [&](ParamID id, double p, double lo, double hi) {
        if (auto* pr = parameters.getParameter(id)) pr->setNormalized(std::clamp(norm(p,lo,hi),0.0,1.0));
    };
    setN(kParamPower, paramPower_.load(), 0.0, 1.0);
    setN(kParamDrive, paramDrive_.load(), kDriveMin, kDriveMax);
    setN(kParamTarget, paramTarget_.load(), kTargetMin, kTargetMax);
    setN(kParamSteepness, paramSteep_.load(), kSteepMin, kSteepMax);
    setN(kParamSmoothing, paramSmooth_.load(), kSmoothMin, kSmoothMax);
    setN(kParamLoopDelay, paramTab1_.load(), kTab1Min, kTab1Max);
    setN(kParamDecorr, paramTab2_.load(), kTab2Min, kTab2Max);
    setN(kParamOutput, paramOutput_.load(), kOutMin, kOutMax);
    return kResultOk;
}

tresult PLUGIN_API DSLARProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double vals[8] = { paramPower_.load(), paramDrive_.load(), paramTarget_.load(),
                       paramSteep_.load(), paramSmooth_.load(), paramTab1_.load(),
                       paramTab2_.load(), paramOutput_.load() };
    for (int i = 0; i < 8; ++i) if (!s.writeDouble(vals[i])) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API DSLARProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "dslar.uidesc");
    return nullptr;
}

// UI-only ramped reset button, wired to the "DslarReset" placeholder in the
// uidesc. Colours resolve from the same palette as the rest of the GUI.
VSTGUI::CView* PLUGIN_API DSLARProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& /*attributes*/,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* /*editor*/) {
    if (name && std::string(name) == "DslarReset") {
        VSTGUI::CColor frame = VSTGUI::kGreyCColor;
        VSTGUI::CColor idle  = VSTGUI::kBlackCColor;
        VSTGUI::CColor azure(0x4a, 0x9e, 0xc8, 0xff);
        if (description) {
            description->getColor("TextDim", frame);
            description->getColor("BgDark", idle);
            description->getColor("SliderActive", azure);
        }
        return new DslarResetButton(VSTGUI::CRect(0, 0, 20, 20), this, frame, idle, azure);
    }
    return nullptr;
}

void DSLARProcessor::readParameterChanges(ProcessData& data) {
    auto* changes = data.inputParameterChanges;
    if (!changes) return;
    int32 n = changes->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        auto* q = changes->getParameterData(i);
        if (!q) continue;
        int32 cnt = q->getPointCount();
        if (cnt <= 0) continue;
        ParamValue v; int32 off;
        if (q->getPoint(cnt - 1, off, v) != kResultOk) continue;
        switch (q->getParameterId()) {
            case kParamPower:     paramPower_.store(v >= 0.5 ? 1.0 : 0.0); break;
            case kParamDrive:     paramDrive_.store(std::clamp(denorm(v, kDriveMin, kDriveMax), kDriveMin, kDriveMax)); break;
            case kParamTarget:    paramTarget_.store(std::clamp(denorm(v, kTargetMin, kTargetMax), kTargetMin, kTargetMax)); break;
            case kParamSteepness: paramSteep_.store(std::clamp(denorm(v, kSteepMin, kSteepMax), kSteepMin, kSteepMax)); break;
            case kParamSmoothing: paramSmooth_.store(std::clamp(denorm(v, kSmoothMin, kSmoothMax), kSmoothMin, kSmoothMax)); break;
            case kParamLoopDelay: paramTab1_.store(std::clamp(denorm(v, kTab1Min, kTab1Max), kTab1Min, kTab1Max)); break;
            case kParamDecorr:    paramTab2_.store(std::clamp(denorm(v, kTab2Min, kTab2Max), kTab2Min, kTab2Max)); break;
            case kParamOutput:    paramOutput_.store(std::clamp(denorm(v, kOutMin, kOutMax), kOutMin, kOutMax)); break;
            default: break;
        }
    }
}

template <typename SampleType>
void DSLARProcessor::processBlock(SampleType** in, SampleType** out, int numSamples) {
    for (int s = 0; s < numSamples; ++s)
        out[0][s] = (SampleType) larsen_.process((double) in[0][s]);
}

template void DSLARProcessor::processBlock<float>(float**, float**, int);
template void DSLARProcessor::processBlock<double>(double**, double**, int);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::DSLARProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM DSLAR", Vst::kDistributable,
               "Fx", FULL_VERSION_STR, kVstVersionString,
               Seam::DSLARProcessor::createInstance)
END_FACTORY
