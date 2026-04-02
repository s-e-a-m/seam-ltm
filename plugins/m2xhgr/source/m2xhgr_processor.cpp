//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · M2XHGR — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "m2xhgr_processor.h"
#include "m2xhgr_ids.h"
#include "version.h"
#include "seam_haar.h"
#include "seam_rotation.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Seam {

using namespace Steinberg;
using namespace Steinberg::Vst;

//─────────────────────────────────────────────────────────────────────────────
// Construction
//─────────────────────────────────────────────────────────────────────────────

M2XHGRProcessor::M2XHGRProcessor ()
{
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API M2XHGRProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // Mono input, first-order AmbiX output (4 channels)
    addAudioInput  (STR16 ("Mono In"),   SpeakerArr::kMono);
    addAudioOutput (STR16 ("AmbiX Out"), SpeakerArr::kAmbi1stOrderACN);

    parameters.addParameter (
        new RangeParameter (STR16 ("Yaw (Z)"),   kParamYaw,   STR16 ("\u00B0"),
                            -180.0, 180.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter (
        new RangeParameter (STR16 ("Pitch (Y)"), kParamPitch, STR16 ("\u00B0"),
                            -180.0, 180.0, 0.0, 0, ParameterInfo::kCanAutomate));
    parameters.addParameter (
        new RangeParameter (STR16 ("Roll (X)"),  kParamRoll,  STR16 ("\u00B0"),
                            -180.0, 180.0, 0.0, 0, ParameterInfo::kCanAutomate));

    return kResultOk;
}

tresult PLUGIN_API M2XHGRProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Activation — reset Haar filter state
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API M2XHGRProcessor::setActive (TBool state)
{
    if (state)
        haar.reset ();
    return SingleComponentEffect::setActive (state);
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API M2XHGRProcessor::process (ProcessData& data)
{
    //--- Read parameter changes ---
    if (data.inputParameterChanges)
    {
        int32 numChanges = data.inputParameterChanges->getParameterCount ();
        for (int32 i = 0; i < numChanges; ++i)
        {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData (i);
            if (!queue)
                continue;

            ParamID id = queue->getParameterId ();
            int32 numPoints = queue->getPointCount ();
            ParamValue value;
            int32 sampleOffset;

            if (queue->getPoint (numPoints - 1, sampleOffset, value) == kResultOk)
            {
                switch (id)
                {
                    case kParamYaw:
                        fYaw = (-180.0 + value * 360.0) * M_PI / 180.0;
                        break;
                    case kParamPitch:
                        fPitch = (-180.0 + value * 360.0) * M_PI / 180.0;
                        break;
                    case kParamRoll:
                        fRoll = (-180.0 + value * 360.0) * M_PI / 180.0;
                        break;
                }
            }
        }
    }

    //--- Guard ---
    if (data.numInputs == 0 || data.numOutputs == 0)
        return kResultOk;

    int32 outChannels = data.outputs[0].numChannels;
    uint32 sampleFramesSize = getSampleFramesSizeInBytes (processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer (processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer (processSetup, data.outputs[0]);

    // Silence passthrough
    if (data.inputs[0].silenceFlags == getChannelMask (data.inputs[0].numChannels))
    {
        data.outputs[0].silenceFlags = getChannelMask (outChannels);
        for (int32 i = 0; i < outChannels; ++i)
            memset (out[i], 0, sampleFramesSize);
        return kResultOk;
    }

    data.outputs[0].silenceFlags = 0;

    if (outChannels < 4)
    {
        for (int32 ch = 0; ch < outChannels; ++ch)
            memset (out[ch], 0, sampleFramesSize);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32)
        processHaarMono<Sample32> (reinterpret_cast<Sample32*>(in[0]),
                                   reinterpret_cast<Sample32**>(out),
                                   data.numSamples);
    else
        processHaarMono<Sample64> (reinterpret_cast<Sample64*>(in[0]),
                                   reinterpret_cast<Sample64**>(out),
                                   data.numSamples);

    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// DSP core — Mono → Haar decomposition → rotation
//
// For each sample:
//   1. Haar wavelet decomposition (haarmn(1)) → 4 spatial components
//   2. rotateYPR → oriented AmbiX output
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void M2XHGRProcessor::processHaarMono (SampleType* in, SampleType** out, int32 numSamples)
{
    const SampleType yaw   = static_cast<SampleType>(fYaw);
    const SampleType pitch = static_cast<SampleType>(fPitch);
    const SampleType roll  = static_cast<SampleType>(fRoll);

    SampleType* out0 = out[0];
    SampleType* out1 = out[1];
    SampleType* out2 = out[2];
    SampleType* out3 = out[3];

    for (int32 i = 0; i < numSamples; ++i)
    {
        // Haar decomposition (uses double state internally)
        double d = static_cast<double>(in[i]);
        double da0, da1, da2, da3;
        haarDecompose (haar, d, da0, da1, da2, da3);

        SampleType a0 = static_cast<SampleType>(da0);
        SampleType a1 = static_cast<SampleType>(da1);
        SampleType a2 = static_cast<SampleType>(da2);
        SampleType a3 = static_cast<SampleType>(da3);

        // Rotate
        rotateYPR (yaw, pitch, roll,
                   a0, a1, a2, a3,
                   out0[i], out1[i], out2[i], out3[i]);
    }
}

//─────────────────────────────────────────────────────────────────────────────
// State
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API M2XHGRProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    double saved[3];
    if (state->read (saved, sizeof (saved)) != kResultOk)
        return kResultFalse;

    fYaw   = (-180.0 + saved[0] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;

    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[2]);

    return kResultOk;
}

tresult PLUGIN_API M2XHGRProcessor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    double saved[3];
    saved[0] = parameters.getParameter (kParamYaw)   ? parameters.getParameter (kParamYaw)->getNormalized ()   : 0.5;
    saved[1] = parameters.getParameter (kParamPitch) ? parameters.getParameter (kParamPitch)->getNormalized () : 0.5;
    saved[2] = parameters.getParameter (kParamRoll)  ? parameters.getParameter (kParamRoll)->getNormalized ()  : 0.5;

    state->write (saved, sizeof (saved));
    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// Bus arrangement — mono in, 4-channel AmbiX out
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API M2XHGRProcessor::setBusArrangements (
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 &&
        SpeakerArr::getChannelCount (inputs[0])  == 1 &&
        SpeakerArr::getChannelCount (outputs[0]) == 4)
    {
        return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// Sample size support
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API M2XHGRProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API M2XHGRProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "m2xhgr.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::M2XHGRProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM M2XHGR",
        0,
        "Fx|Spatial",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::M2XHGRProcessor::createInstance)

END_FACTORY
