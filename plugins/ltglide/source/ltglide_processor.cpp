#include "ltglide_processor.h"
#include "ltglide_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

// Logarithmic frequency parameter (equal normalized travel = equal octaves).
class LogRangeParameter : public Steinberg::Vst::RangeParameter {
public:
    LogRangeParameter(const TChar* title, ParamID tag, const TChar* units,
                      ParamValue minPlain, ParamValue maxPlain,
                      ParamValue defaultValuePlain,
                      int32 stepCount = 0,
                      int32 flags = ParameterInfo::kCanAutomate)
        : RangeParameter(title, tag, units, minPlain, maxPlain,
                         defaultValuePlain, stepCount, flags)
    {
        const ParamValue n = LogRangeParameter::toNormalized(defaultValuePlain);
        info.defaultNormalizedValue = n;
        setNormalized(n);
    }
    ParamValue toPlain(ParamValue norm) const SMTG_OVERRIDE {
        const double lo = getMin(), hi = getMax();
        return lo * std::pow(hi / lo, norm);
    }
    ParamValue toNormalized(ParamValue plain) const SMTG_OVERRIDE {
        const double lo = getMin(), hi = getMax();
        return std::log(plain / lo) / std::log(hi / lo);
    }
};

static inline double linNormToPlain(double v, double lo, double hi) { return v * (hi - lo) + lo; }
static inline double linPlainToNorm(double p, double lo, double hi) { return (p - lo) / (hi - lo); }
static inline double logNormToPlain(double v, double lo, double hi) { return lo * std::pow(hi / lo, v); }
static inline double logPlainToNorm(double p, double lo, double hi) { return std::log(p / lo) / std::log(hi / lo); }

LTGLIDEProcessor::LTGLIDEProcessor() {}

tresult PLUGIN_API LTGLIDEProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioOutput(STR16("Output"), SpeakerArr::kMono);

    auto* lv = new RangeParameter(STR16("Level"), kParamLevel, STR16("dBFS"),
        kLevelMinDb, kLevelMaxDb, kLevelDefaultDb, 0, ParameterInfo::kCanAutomate);
    lv->setPrecision(1);
    parameters.addParameter(lv);

    auto* f0 = new LogRangeParameter(STR16("F0"), kParamF0, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, kF0DefaultHz, 0, ParameterInfo::kCanAutomate);
    f0->setPrecision(0);
    parameters.addParameter(f0);

    auto* f1 = new LogRangeParameter(STR16("F1"), kParamF1, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, kF1DefaultHz, 0, ParameterInfo::kCanAutomate);
    f1->setPrecision(0);
    parameters.addParameter(f1);

    auto* sm = new StringListParameter(STR16("Sweep"), kParamSmode);
    sm->appendString(STR16("linear"));
    sm->appendString(STR16("exponential"));
    sm->getInfo().defaultNormalizedValue = 1.0;   // exponential is the default
    sm->setNormalized(1.0);
    parameters.addParameter(sm);

    auto* dm = new StringListParameter(STR16("Timing"), kParamDmode);
    dm->appendString(STR16("passo"));
    dm->appendString(STR16("gap"));
    dm->getInfo().defaultNormalizedValue = 1.0;   // gap is the default
    dm->setNormalized(1.0);
    parameters.addParameter(dm);

    auto* dl = new RangeParameter(STR16("Delta"), kParamDelta, STR16("s"),
        kDeltaMinSec, kDeltaMaxSec, kDeltaDefaultSec, 0, ParameterInfo::kCanAutomate);
    dl->setPrecision(2);
    parameters.addParameter(dl);

    auto* tt = new RangeParameter(STR16("Sweep Time"), kParamT, STR16("s"),
        kTMinSec, kTMaxSec, kTDefaultSec, 0, ParameterInfo::kCanAutomate);
    tt->setPrecision(1);
    parameters.addParameter(tt);

    auto* lp = new RangeParameter(STR16("Loop"), kParamLoop, nullptr,
        0, 1, 0, 1, ParameterInfo::kCanAutomate);
    parameters.addParameter(lp);

    auto* stoneParam = new StringListParameter(
        STR16("STONE"), kParamStoneId, STR16(""),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    stoneParam->appendString(STR16("?"));
    stoneParam->appendString(STR16("1"));
    stoneParam->appendString(STR16("2"));
    stoneParam->appendString(STR16("3"));
    stoneParam->appendString(STR16("4"));
    stoneParam->appendString(STR16("5"));
    stoneParam->appendString(STR16("6"));
    stoneParam->appendString(STR16("7"));
    stoneParam->appendString(STR16("8"));
    parameters.addParameter(stoneParam);

    return kResultOk;
}

tresult PLUGIN_API LTGLIDEProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API LTGLIDEProcessor::setActive(TBool state) {
    if (state) {
        glide_.setDelta(paramDeltaSec_.load());
        glide_.setDmode(paramDmode_.load());
        glide_.reset();
        transport_.setSweepSeconds(paramTSec_.load());
        transport_.setLoop(paramLoop_.load());
        transport_.reset();               // never resume a pass mid-way on (re)activation
        prevGainLin_ = computeGainLin();

        busHandle_ = CalbusClient::instance().registerSlot();
        // (Re)activation always lands in Idle (see transport_.reset() above,
        // which never resumes a pass mid-way), so there is no pass to anchor
        // yet; reset the edge-detect state so a reactivated instance does not
        // carry a stale anchor or a stale running/idle edge from before.
        busAnchor_.reset(transport_.passCount());
        // NOTE (Spec 3 receiver): passCounter is monotone by design and is
        // NOT reset above, so on a second-or-later activation this call pairs
        // a carried-over passCounter (>0) with passStartSample=-1. That is
        // only harmless if the receiver acts on passCounter INCREASES rather
        // than its absolute value -- which is the documented reason the
        // counter is monotone in the first place. Left as-is (behaviour
        // unchanged); flagging here for whoever implements that receiver.
        publishBusRecord(-1);
    } else {
        CalbusClient::instance().unregisterSlot(busHandle_);
        busHandle_ = SEAM_CALBUS_NO_HANDLE;
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API LTGLIDEProcessor::setupProcessing(ProcessSetup& setup) {
    glide_.prepare(setup.sampleRate);
    glide_.setDelta(paramDeltaSec_.load());
    glide_.setDmode(paramDmode_.load());
    transport_.prepare(setup.sampleRate);
    transport_.setSweepSeconds(paramTSec_.load());
    transport_.setLoop(paramLoop_.load());
    return SingleComponentEffect::setupProcessing(setup);
}

void LTGLIDEProcessor::publishBusRecord(int64_t hostStartSample) {
    if (busHandle_ == SEAM_CALBUS_NO_HANDLE) return;

    SeamCalbusRecord r{};
    r.kind    = kSeamCalbusGlide;
    r.stoneId = (uint32_t)paramStoneId_.load();
    r.active  = transport_.running() ? 1u : 0u;
    r.levelDb = paramLevelDb_.load();
    r.sampleRate = processSetup.sampleRate;
    r.u.glide.passCounter     = transport_.passCount();
    r.u.glide.passStartSample = hostStartSample;
    r.u.glide.f0          = paramF0Hz_.load();
    r.u.glide.f1          = paramF1Hz_.load();
    r.u.glide.durationSec = paramTSec_.load();
    r.u.glide.deltaSec    = paramDeltaSec_.load();
    r.u.glide.sweepMode   = (uint32_t)paramSmode_.load();
    r.u.glide.diracMode   = (uint32_t)paramDmode_.load();

    CalbusClient::instance().publish(busHandle_, r);
}

tresult PLUGIN_API LTGLIDEProcessor::process(ProcessData& data) {
    readParameterChanges(data);
    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;

    glide_.setDelta(paramDeltaSec_.load());
    glide_.setDmode(paramDmode_.load());
    transport_.setSweepSeconds(paramTSec_.load());
    transport_.setLoop(paramLoop_.load());

    // ProcessContext::continousTimeSamples [sic — the SDK header spells it
    // without the second 'u'] is an OPTIONAL anchor: the host declares its
    // validity with kContTimeValid, and processContext may be null outright.
    // All of Spec 3's synchronisation rests on this field, so detect its
    // absence and say so (-1 -> strx prints "no host clock") instead of
    // silently anchoring passes to a fictional zero.
    int64_t blockStart = -1;
    if (data.processContext &&
        (data.processContext->state & ProcessContext::kContTimeValid)) {
        blockStart = (int64_t)data.processContext->continousTimeSamples;
    }

    int numChannels = data.outputs[0].numChannels;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32)
        processBlock<float>((float**)out, numChannels, data.numSamples, blockStart);
    else
        processBlock<double>((double**)out, numChannels, data.numSamples, blockStart);
    return kResultOk;
}

tresult PLUGIN_API LTGLIDEProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API LTGLIDEProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) {
    if (numIns != 0 || numOuts != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(outs[0]) != 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API LTGLIDEProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double level = kLevelDefaultDb, f0 = kF0DefaultHz, f1 = kF1DefaultHz;
    double delta = kDeltaDefaultSec, t = kTDefaultSec;
    int32 smode = 1, dmode = 1, loop = 0;
    if (!s.readDouble(level)) return kResultFalse;
    if (!s.readDouble(f0))    return kResultFalse;
    if (!s.readDouble(f1))    return kResultFalse;
    if (!s.readInt32(smode))  return kResultFalse;
    if (!s.readInt32(dmode))  return kResultFalse;
    if (!s.readDouble(delta)) return kResultFalse;
    if (!s.readDouble(t))     return kResultFalse;
    if (!s.readInt32(loop))   return kResultFalse;
    // stoneId gets a local default like every other parameter above: if the
    // read fails (pre-calbus preset) or the range is invalid, this stores 0
    // (undeclared) unconditionally, rather than leaving whatever stoneId the
    // instance already had in place -- a stale STONE id would attribute a
    // calibration pass to a loudspeaker the loaded preset never named.
    int32 stoneId = 0;
    paramStoneId_.store(s.readInt32(stoneId) && stoneId >= 0 && stoneId <= 8 ? stoneId : 0);

    paramLevelDb_.store(std::clamp(level, kLevelMinDb, kLevelMaxDb));
    paramF0Hz_.store(std::clamp(f0, kFreqMinHz, kFreqMaxHz));
    paramF1Hz_.store(std::clamp(f1, kFreqMinHz, kFreqMaxHz));
    paramSmode_.store((smode != 0) ? 1 : 0);
    paramDmode_.store((dmode != 0) ? 1 : 0);
    paramDeltaSec_.store(std::clamp(delta, kDeltaMinSec, kDeltaMaxSec));
    paramTSec_.store(std::clamp(t, kTMinSec, kTMaxSec));
    paramLoop_.store(loop != 0);

    if (auto* p = parameters.getParameter(kParamLevel))
        p->setNormalized(linPlainToNorm(paramLevelDb_.load(), kLevelMinDb, kLevelMaxDb));
    if (auto* p = parameters.getParameter(kParamF0))
        p->setNormalized(std::clamp(logPlainToNorm(paramF0Hz_.load(), kFreqMinHz, kFreqMaxHz), 0.0, 1.0));
    if (auto* p = parameters.getParameter(kParamF1))
        p->setNormalized(std::clamp(logPlainToNorm(paramF1Hz_.load(), kFreqMinHz, kFreqMaxHz), 0.0, 1.0));
    if (auto* p = parameters.getParameter(kParamSmode))
        p->setNormalized(paramSmode_.load() ? 1.0 : 0.0);
    if (auto* p = parameters.getParameter(kParamDmode))
        p->setNormalized(paramDmode_.load() ? 1.0 : 0.0);
    if (auto* p = parameters.getParameter(kParamDelta))
        p->setNormalized(linPlainToNorm(paramDeltaSec_.load(), kDeltaMinSec, kDeltaMaxSec));
    if (auto* p = parameters.getParameter(kParamT))
        p->setNormalized(linPlainToNorm(paramTSec_.load(), kTMinSec, kTMaxSec));
    if (auto* p = parameters.getParameter(kParamLoop))
        p->setNormalized(paramLoop_.load() ? 1.0 : 0.0);
    if (auto* p = parameters.getParameter(kParamStoneId))
        p->setNormalized((double)paramStoneId_.load() / (kStoneIdStepCount - 1));

    return kResultOk;
}

tresult PLUGIN_API LTGLIDEProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    if (!s.writeDouble(paramLevelDb_.load())) return kResultFalse;
    if (!s.writeDouble(paramF0Hz_.load()))    return kResultFalse;
    if (!s.writeDouble(paramF1Hz_.load()))    return kResultFalse;
    if (!s.writeInt32(paramSmode_.load()))    return kResultFalse;
    if (!s.writeInt32(paramDmode_.load()))    return kResultFalse;
    if (!s.writeDouble(paramDeltaSec_.load()))return kResultFalse;
    if (!s.writeDouble(paramTSec_.load()))    return kResultFalse;
    if (!s.writeInt32(paramLoop_.load() ? 1 : 0)) return kResultFalse;
    if (!s.writeInt32(paramStoneId_.load())) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API LTGLIDEProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "ltglide.uidesc");
    return nullptr;
}

double LTGLIDEProcessor::computeGainLin() const {
    return std::pow(10.0, paramLevelDb_.load() / 20.0);
}

void LTGLIDEProcessor::readParameterChanges(ProcessData& data) {
    auto* changes = data.inputParameterChanges;
    if (!changes) return;
    int32 n = changes->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        auto* q = changes->getParameterData(i);
        if (!q) continue;
        ParamID id = q->getParameterId();
        int32 cnt = q->getPointCount();
        if (cnt <= 0) continue;
        ParamValue v; int32 off;
        if (q->getPoint(cnt - 1, off, v) != kResultOk) continue;
        switch (id) {
            case kParamLevel: paramLevelDb_.store(std::clamp(linNormToPlain(v, kLevelMinDb, kLevelMaxDb), kLevelMinDb, kLevelMaxDb)); break;
            case kParamF0:    paramF0Hz_.store(std::clamp(logNormToPlain(v, kFreqMinHz, kFreqMaxHz), kFreqMinHz, kFreqMaxHz)); break;
            case kParamF1:    paramF1Hz_.store(std::clamp(logNormToPlain(v, kFreqMinHz, kFreqMaxHz), kFreqMinHz, kFreqMaxHz)); break;
            case kParamSmode: paramSmode_.store(v >= 0.5 ? 1 : 0); break;
            case kParamDmode: paramDmode_.store(v >= 0.5 ? 1 : 0); break;
            case kParamDelta: paramDeltaSec_.store(std::clamp(linNormToPlain(v, kDeltaMinSec, kDeltaMaxSec), kDeltaMinSec, kDeltaMaxSec)); break;
            case kParamT:     paramTSec_.store(std::clamp(linNormToPlain(v, kTMinSec, kTMaxSec), kTMinSec, kTMaxSec)); break;
            case kParamLoop:  paramLoop_.store(v >= 0.5); break;
            case kParamStoneId:
                paramStoneId_.store(std::clamp((int)(v * (kStoneIdStepCount - 1) + 0.5), 0, kStoneIdStepCount - 1));
                break;
        }
    }
}

template <typename SampleType>
void LTGLIDEProcessor::processBlock(SampleType** outputs, int numChannels, int numSamples,
                                    int64_t blockStartSample) {
    const double targetGain = computeGainLin();
    const double startGain  = prevGainLin_;
    const double gainStep   = (numSamples > 0) ? (targetGain - startGain) / numSamples : 0.0;

    const double f0 = paramF0Hz_.load();
    const double f1 = paramF1Hz_.load();
    const int    sm = paramSmode_.load();

    for (int s = 0; s < numSamples; ++s) {
        const double g = startGain + gainStep * s;      // per-block gain ramp
        double y = 0.0;
        auto tick = transport_.process();
        switch (tick.kind) {
            case ltglide::GlideTransport::Kind::Dirac:
                y = 1.0;                                 // unit impulse (scaled by g)
                break;
            case ltglide::GlideTransport::Kind::Glide: {
                if (tick.p == 0.0) glide_.reset();       // deterministic per-pass start
                const double f = ltglide::SweepFreq(f0, f1, sm, tick.p);
                y = glide_.process(f);
                break;
            }
            case ltglide::GlideTransport::Kind::Silence:
            default:
                y = 0.0;
                break;
        }
        const SampleType out = (SampleType)(y * g);
        for (int c = 0; c < numChannels; ++c)
            outputs[c][s] = out;

        // The tick that opens a pass is the head Dirac itself, so `s` is the
        // pass's exact sample offset within this block. BusAnchor owns the
        // pass-start detection and the anchor latch (see ltglide_dsp.h;
        // unit-tested in tests/ltglide_dsp_test.cpp).
        int64_t anchor = -1;
        if (busAnchor_.onSample(transport_.passCount(), blockStartSample, s, &anchor))
            publishBusRecord(anchor);
    }
    prevGainLin_ = targetGain;

    // Edge-detect the run/idle transition instead of publishing every idle
    // block. -1 has exactly one contracted meaning (seam_calbus.h: "the host
    // gave no valid continuous clock"), so an idle heartbeat that republished
    // passStartSample=-1 every block would make a COMPLETED pass (passCounter
    // already at N) indistinguishable from a pass that was never anchored.
    // BusAnchor::onRunningChanged republishes the LATCHED anchor of the pass
    // that just finished instead of -1 — which is exactly what Spec 3 reads
    // after a capture ends. A mid-loop flicker (Wait->Idle->beginPass landing
    // on the last sample of a block) now just republishes the same record
    // twice instead of emitting a false unanchored one.
    int64_t anchor = -1;
    if (busAnchor_.onRunningChanged(transport_.running(), &anchor))
        publishBusRecord(anchor);
}

template void LTGLIDEProcessor::processBlock<float>(float**, int, int, int64_t);
template void LTGLIDEProcessor::processBlock<double>(double**, int, int, int64_t);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::LTGLIDEProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM LTGLIDE", Vst::kDistributable,
               "Instrument|Synth", FULL_VERSION_STR, kVstVersionString,
               Seam::LTGLIDEProcessor::createInstance)
END_FACTORY
