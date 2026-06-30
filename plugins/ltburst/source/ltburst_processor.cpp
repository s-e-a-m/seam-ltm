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
    using RangeParameter::RangeParameter;
    Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue norm) const SMTG_OVERRIDE {
        const double lo = getMin(), hi = getMax();
        return lo * std::pow(hi / lo, norm);
    }
    Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plain) const SMTG_OVERRIDE {
        const double lo = getMin(), hi = getMax();
        return std::log(plain / lo) / std::log(hi / lo);
    }
};

LTBURSTProcessor::LTBURSTProcessor() {}

tresult PLUGIN_API LTBURSTProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

    auto* refParam = new StringListParameter(
        STR16("Reference"), kParamReference, STR16("dBFS RMS"),
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
    refParam->appendString(STR16("-23"));
    refParam->appendString(STR16("-20"));
    refParam->appendString(STR16("-18"));
    parameters.addParameter(refParam);

    parameters.addParameter(new RangeParameter(
        STR16("Trim"), kParamTrim, STR16("dB"),
        -6.0, 6.0, 0.0, 0,
        ParameterInfo::kCanAutomate));

    parameters.addParameter(new LogRangeParameter(
        STR16("Frequency"), kParamFrequency, STR16("Hz"),
        kFreqMinHz, kFreqMaxHz, 1000.0, 0,
        ParameterInfo::kCanAutomate));

    parameters.addParameter(new RangeParameter(
        STR16("Dwell"), kParamDwell, STR16("ms"),
        kDwellMinMs, kDwellMaxMs, 300.0, 0,
        ParameterInfo::kCanAutomate));

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
    if (numIns != 0 || numOuts != 1) return kResultFalse;
    int channels = SpeakerArr::getChannelCount(outs[0]);
    if (channels < 1) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API LTBURSTProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    int32 refIdx = 0; double trim = 0.0; double freq = 1000.0; double dwell = 300.0;
    if (!s.readInt32(refIdx))  return kResultFalse;
    if (!s.readDouble(trim))   return kResultFalse;
    if (!s.readDouble(freq))   return kResultFalse;
    if (!s.readDouble(dwell))  return kResultFalse;

    paramReferenceIdx_.store(std::clamp<int>(refIdx, 0, kReferenceStepCount - 1));
    paramTrimDb_.store(std::clamp(trim, -6.0, 6.0));
    paramFreqHz_.store(std::clamp(freq, kFreqMinHz, kFreqMaxHz));
    paramDwellMs_.store(std::clamp(dwell, kDwellMinMs, kDwellMaxMs));

    if (auto* p = parameters.getParameter(kParamReference))
        p->setNormalized((double)paramReferenceIdx_.load() / (kReferenceStepCount - 1));
    if (auto* p = parameters.getParameter(kParamTrim))
        p->setNormalized((paramTrimDb_.load() + 6.0) / 12.0);
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
    int32 refIdx = paramReferenceIdx_.load();
    double trim  = paramTrimDb_.load();
    double freq  = paramFreqHz_.load();
    double dwell = paramDwellMs_.load();
    if (!s.writeInt32(refIdx)) return kResultFalse;
    if (!s.writeDouble(trim))  return kResultFalse;
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
    int idx = std::clamp(paramReferenceIdx_.load(), 0, kReferenceStepCount - 1);
    double db = kReferenceLevelsDb[idx] + paramTrimDb_.load() + kCalibrationOffsetDb;
    return std::pow(10.0, db / 20.0);
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
            case kParamReference: {
                int idx = (int)std::round(v * (kReferenceStepCount - 1));
                paramReferenceIdx_.store(std::clamp(idx, 0, kReferenceStepCount - 1));
            } break;
            case kParamTrim:
                paramTrimDb_.store(v * 12.0 - 6.0);
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
            outputs[c][s] = y;                          // mono duplicated to all channels
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
               "Instrument|Synth", FULL_VERSION_STR, kVstVersionString,
               Seam::LTBURSTProcessor::createInstance)
END_FACTORY
