//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ADDELAY — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "addelay_processor.h"
#include "addelay_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cmath>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

AddelayProcessor::AddelayProcessor() {}

tresult PLUGIN_API AddelayProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    // setPrecision controls the decimals the host shows in the value readout;
    // Vst::Parameter defaults to 4, which is more than these quantities carry.
    auto* dist = new RangeParameter(
        STR16("Distance"), kParamDistance, STR16("m"),
        0.0, kAddDistMax, 0.0, 0, ParameterInfo::kCanAutomate);
    dist->setPrecision(3);                       // metres to the millimetre (matches ddelay)
    parameters.addParameter(dist);
    auto* temp = new RangeParameter(
        STR16("Temperature"), kParamTemperature, STR16("C"),
        kAddTempMin, kAddTempMax, 20.0, 0, ParameterInfo::kCanAutomate);
    temp->setPrecision(1);                       // 0.1 degC
    parameters.addParameter(temp);
    auto* humid = new RangeParameter(
        STR16("Humidity"), kParamHumidity, STR16("%"),
        kAddRhMin, kAddRhMax, 50.0, 0, ParameterInfo::kCanAutomate);
    humid->setPrecision(1);                      // 0.1 %
    parameters.addParameter(humid);

    auto* topo = new StringListParameter(STR16("Topology"), kParamTopology);
    topo->appendString(STR16("Shelf"));
    topo->appendString(STR16("Cascade"));
    parameters.addParameter(topo);

    auto* spread = new StringListParameter(STR16("Spreading"), kParamSpreading);
    spread->appendString(STR16("Off"));
    spread->appendString(STR16("On"));
    parameters.addParameter(spread);

    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::terminate() {
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API AddelayProcessor::setupProcessing(ProcessSetup& setup) {
    tresult res = SingleComponentEffect::setupProcessing(setup);
    dsp_.prepare(processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0);
    return res;
}

tresult PLUGIN_API AddelayProcessor::setActive(TBool state) {
    if (state) {
        dsp_.prepare(processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0);
        lastD_ = lastT_ = lastRh_ = -1e9;      // force a re-design on first block
        lastTopo_ = lastSpread_ = -1;
        applyParams();                          // honour recalled state
        dsp_.reset();
    }
    return SingleComponentEffect::setActive(state);
}

void AddelayProcessor::applyParams() {
    auto norm = [&](ParamID id) -> double {
        auto* p = parameters.getParameter(id);
        return p ? p->getNormalized() : 0.0;
    };
    const double d  = norm(kParamDistance)    * kAddDistMax;
    const double t  = kAddTempMin + norm(kParamTemperature) * (kAddTempMax - kAddTempMin);
    const double rh = kAddRhMin   + norm(kParamHumidity)    * (kAddRhMax   - kAddRhMin);
    const int topo   = norm(kParamTopology)  >= 0.5 ? 1 : 0;
    const int spread = norm(kParamSpreading) >= 0.5 ? 1 : 0;

    // Dirty-check on the mm/quantized triple + discrete choices.
    const double dq = std::round(d * 1000.0) / 1000.0;
    if (dq == lastD_ && t == lastT_ && rh == lastRh_ &&
        topo == lastTopo_ && spread == lastSpread_)
        return;
    lastD_ = dq; lastT_ = t; lastRh_ = rh; lastTopo_ = topo; lastSpread_ = spread;

    dsp_.setParams(d, t, rh,
                   topo ? air::Topology::Cascade : air::Topology::Shelf,
                   spread != 0);
}

tresult PLUGIN_API AddelayProcessor::process(ProcessData& data) {
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
    applyParams();   // one re-design per block at most (dirty-checked); also
                      // picks up a preset recalled via setState() on the UI thread.

    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;

    const int32 inCh  = data.inputs[0].numChannels;
    const int32 outCh = data.outputs[0].numChannels;
    const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    if (inCh < 4 || outCh < 4) {
        for (int32 c = 0; c < outCh; ++c) if (out[c]) memset(out[c], 0, bytes);
        return kResultOk;
    }
    data.outputs[0].silenceFlags = 0;   // IIR tail: never claim silence out

    if (data.symbolicSampleSize == kSample32)
        dsp_.process(reinterpret_cast<const float* const*>(in),
                     reinterpret_cast<float* const*>(out), data.numSamples);
    else
        dsp_.process(reinterpret_cast<const double* const*>(in),
                     reinterpret_cast<double* const*>(out), data.numSamples);
    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// State: five normalized floats, fixed order. Read defensively (short-read
// caveat, memory project_vst3_state_shortread_rotation_family): a missing
// field keeps the parameter's current default rather than corrupting it.
tresult PLUGIN_API AddelayProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    const ParamID ids[5] = { kParamDistance, kParamTemperature, kParamHumidity,
                             kParamTopology, kParamSpreading };
    for (int i = 0; i < 5; ++i) {
        float v = 0.0f;
        if (!s.readFloat(v)) break;         // stop on short read, keep remaining defaults
        setParamNormalized(ids[i], v);
    }
    // Defer the DSP re-configure to the next process() block (audio thread);
    // never reconfigure from the UI thread.
    lastD_ = lastT_ = lastRh_ = -1e9; lastTopo_ = lastSpread_ = -1;
    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    const ParamID ids[5] = { kParamDistance, kParamTemperature, kParamHumidity,
                             kParamTopology, kParamSpreading };
    for (int i = 0; i < 5; ++i) {
        auto* p = parameters.getParameter(ids[i]);
        s.writeFloat(p ? (float)p->getNormalized() : 0.0f);
    }
    return kResultOk;
}

tresult PLUGIN_API AddelayProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API AddelayProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "addelay.uidesc");
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::AddelayProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM ADDELAY",
        0,
        "Fx|Delay",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::AddelayProcessor::createInstance)
END_FACTORY
