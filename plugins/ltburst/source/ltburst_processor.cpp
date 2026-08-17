#include "ltburst_processor.h"
#include "ltburst_ids.h"
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
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

// Logarithmic frequency parameter: equal normalized travel spans equal
// octaves, while toString still renders Hz (min/max stay 20..20000).
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
        // The base ctor stored the default normalized with the LINEAR taper
        // (C++ virtual dispatch is frozen to the base type during base
        // construction). Re-normalize with the log taper so host
        // "reset to default" restores the intended Hz value.
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

LTBURSTProcessor::LTBURSTProcessor() {}

tresult PLUGIN_API LTBURSTProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // A main input bus we never read. It exists precisely so that the signal
    // arriving at the insert DIES here: with zero input buses a host has
    // nothing to hand the plugin, so it routes the track signal AROUND us and
    // the generator appears to "pass audio through". Declaring the bus puts us
    // back in the chain, and processBlock() overwrites every output sample —
    // so blocking the input is a structural consequence, not a runtime branch.
    addAudioInput (STR16("Input"),  SpeakerArr::kMono);
    addAudioOutput(STR16("Output"), SpeakerArr::kMono);

    auto* lv = new RangeParameter(
        STR16("Level"), kParamLevel, STR16("dBFS"),
        kLevelMinDb, kLevelMaxDb, kLevelDefaultDb, 0,
        ParameterInfo::kCanAutomate);
    lv->setPrecision(1);
    parameters.addParameter(lv);

    auto* fr = new LogRangeParameter(
        STR16("Frequency"), kParamFrequency, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, 1000.0, 0,
        ParameterInfo::kCanAutomate);
    fr->setPrecision(0);
    parameters.addParameter(fr);

    auto* dw = new RangeParameter(
        STR16("Dwell"), kParamDwell, STR16("ms"),
        kDwellMinMs, kDwellMaxMs, 300.0, 0,
        ParameterInfo::kCanAutomate);
    dw->setPrecision(0);
    parameters.addParameter(dw);

    return kResultOk;
}

tresult PLUGIN_API LTBURSTProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API LTBURSTProcessor::setActive(TBool state) {
    if (state) {
        burst_.setFrequency(paramFreqHz_.load());
        burst_.setDwell(paramDwellMs_.load());
        burst_.reset();
        prevGainLin_ = computeGainLin();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API LTBURSTProcessor::setupProcessing(ProcessSetup& setup) {
    burst_.prepare(setup.sampleRate);
    burst_.setFrequency(paramFreqHz_.load());
    burst_.setDwell(paramDwellMs_.load());
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API LTBURSTProcessor::process(ProcessData& data) {
    readParameterChanges(data);

    if (data.numOutputs == 0 || data.numSamples == 0) return kResultOk;

    // A generator is never silent by inheritance. The buffers can arrive still
    // flagged silent from whatever fed the insert, and a host that trusts the
    // flag would skip every plugin downstream of us.
    data.outputs[0].silenceFlags = 0;

    // Apply current parameters to the core (block rate; phase stays continuous).
    burst_.setFrequency(paramFreqHz_.load());
    burst_.setDwell(paramDwellMs_.load());

    int numChannels = data.outputs[0].numChannels;
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
    if (data.symbolicSampleSize == kSample32) {
        processBlock<float>((float**)out, numChannels, data.numSamples);
    } else {
        processBlock<double>((double**)out, numChannels, data.numSamples);
    }
    return kResultOk;
}

tresult PLUGIN_API LTBURSTProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API LTBURSTProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns,
    SpeakerArrangement* outs, int32 numOuts) {
    if (numOuts != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(outs[0]) != 1) return kResultFalse;
    // The input bus is never read, so the only thing to accept or refuse is a
    // shape we declared: one mono input (insert on an audio track) or none at
    // all (instrument track, where the host gives the generator no input).
    if (numIns > 1) return kResultFalse;
    if (numIns == 1 && SpeakerArr::getChannelCount(ins[0]) != 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API LTBURSTProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double level = kLevelDefaultDb; double freq = 1000.0; double dwell = 300.0;
    if (!s.readDouble(level)) return kResultFalse;
    if (!s.readDouble(freq))  return kResultFalse;
    if (!s.readDouble(dwell)) return kResultFalse;

    paramLevelDb_.store(std::clamp(level, kLevelMinDb, kLevelMaxDb));
    paramFreqHz_.store(std::clamp(freq,  kFreqMinHz,  kFreqMaxHz));
    paramDwellMs_.store(std::clamp(dwell, kDwellMinMs, kDwellMaxMs));

    if (auto* p = parameters.getParameter(kParamLevel))
        p->setNormalized((paramLevelDb_.load() - kLevelMinDb) / (kLevelMaxDb - kLevelMinDb));
    if (auto* p = parameters.getParameter(kParamFrequency)) {
        double norm = std::log(paramFreqHz_.load() / kFreqMinHz) / std::log(kFreqMaxHz / kFreqMinHz);
        p->setNormalized(std::clamp(norm, 0.0, 1.0));
    }
    if (auto* p = parameters.getParameter(kParamDwell))
        p->setNormalized((paramDwellMs_.load() - kDwellMinMs) / (kDwellMaxMs - kDwellMinMs));

    return kResultOk;
}

tresult PLUGIN_API LTBURSTProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    double level = paramLevelDb_.load();
    double freq  = paramFreqHz_.load();
    double dwell = paramDwellMs_.load();
    if (!s.writeDouble(level)) return kResultFalse;
    if (!s.writeDouble(freq))  return kResultFalse;
    if (!s.writeDouble(dwell)) return kResultFalse;
    return kResultOk;
}

IPlugView* PLUGIN_API LTBURSTProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "ltburst.uidesc");
    return nullptr;
}

double LTBURSTProcessor::computeGainLin() const {
    // Level is the carrier/peak amplitude in dBFS; the Hann window keeps
    // the actual peak at or below this — a safe ceiling.
    return std::pow(10.0, paramLevelDb_.load() / 20.0);
}

void LTBURSTProcessor::readParameterChanges(ProcessData& data) {
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
            case kParamLevel:
                paramLevelDb_.store(std::clamp(v * (kLevelMaxDb - kLevelMinDb) + kLevelMinDb,
                                               kLevelMinDb, kLevelMaxDb));
                break;
            case kParamFrequency: {
                // Logarithmic de-normalisation: equal travel -> equal octaves.
                double hz = kFreqMinHz * std::pow(kFreqMaxHz / kFreqMinHz, v);
                paramFreqHz_.store(std::clamp(hz, kFreqMinHz, kFreqMaxHz));
            } break;
            case kParamDwell:
                paramDwellMs_.store(std::clamp(v * (kDwellMaxMs - kDwellMinMs) + kDwellMinMs,
                                               kDwellMinMs, kDwellMaxMs));
                break;
        }
    }
}

template <typename SampleType>
void LTBURSTProcessor::processBlock(SampleType** outputs, int numChannels, int numSamples) {
    const double targetGain = computeGainLin();
    const double startGain  = prevGainLin_;
    const double gainStep   = (numSamples > 0) ? (targetGain - startGain) / numSamples : 0.0;

    for (int s = 0; s < numSamples; ++s) {
        double g = startGain + gainStep * s;          // per-block linear gain ramp
        SampleType y = (SampleType)(burst_.process() * g);
        for (int c = 0; c < numChannels; ++c)
            outputs[c][s] = y;                        // write to all channels (mono = 1)
    }
    prevGainLin_ = targetGain;
}

template void LTBURSTProcessor::processBlock<float>(float**, int, int);
template void LTBURSTProcessor::processBlock<double>(double**, int, int);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::LTBURSTProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM LTBURST", Vst::kDistributable,
               "Fx|Generator", FULL_VERSION_STR, kVstVersionString,
               Seam::LTBURSTProcessor::createInstance)
END_FACTORY
