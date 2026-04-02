//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · XYPRrot — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "xyprrot_processor.h"
#include "xyprrot_ids.h"
#include "version.h"
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

XYPRrotProcessor::XYPRrotProcessor ()
{
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization — declare buses and parameters
//
// Three rotation angles, each -180° to +180°, default 0°.
// Internally stored in radians; UI shows degrees.
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API XYPRrotProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // First-order AmbiX: 4 channels (ACN 0,1,2,3 = W,Y,Z,X)
    addAudioInput  (STR16 ("AmbiX In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput (STR16 ("AmbiX Out"), SpeakerArr::kAmbi1stOrderACN);

    // Parameters: angles in degrees, -180 to +180, default 0
    // stepCount = 0 means continuous
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

tresult PLUGIN_API XYPRrotProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API XYPRrotProcessor::process (ProcessData& data)
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

            // Take the last value in the queue (final value for this block)
            if (queue->getPoint (numPoints - 1, sampleOffset, value) == kResultOk)
            {
                // Convert from normalized (0–1) to plain degrees, then to radians
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

    //--- Guard: nothing to process ---
    if (data.numInputs == 0 || data.numOutputs == 0)
        return kResultOk;

    int32 numChannels = data.inputs[0].numChannels;
    uint32 sampleFramesSize = getSampleFramesSizeInBytes (processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer (processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer (processSetup, data.outputs[0]);

    // Silence passthrough
    if (data.inputs[0].silenceFlags == getChannelMask (data.inputs[0].numChannels))
    {
        data.outputs[0].silenceFlags = data.inputs[0].silenceFlags;
        for (int32 i = 0; i < numChannels; ++i)
            if (in[i] != out[i])
                memset (out[i], 0, sampleFramesSize);
        return kResultOk;
    }

    data.outputs[0].silenceFlags = 0;

    // Need exactly 4 channels for first-order AmbiX
    if (numChannels < 4)
    {
        for (int32 ch = 0; ch < numChannels; ++ch)
            if (in[ch] != out[ch])
                memcpy (out[ch], in[ch], sampleFramesSize);
        return kResultOk;
    }

    // Apply rotation
    if (data.symbolicSampleSize == kSample32)
        processRotation<Sample32> (reinterpret_cast<Sample32**>(in),
                                   reinterpret_cast<Sample32**>(out),
                                   data.numSamples);
    else
        processRotation<Sample64> (reinterpret_cast<Sample64**>(in),
                                   reinterpret_cast<Sample64**>(out),
                                   data.numSamples);

    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// DSP core — first-order AmbiX rotation
//
// rotateYPR from seam_rotation.h applies Roll → Pitch → Yaw in intrinsic
// order, matching the Faust reference in test2/XYPRrot.dsp.
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void XYPRrotProcessor::processRotation (SampleType** in, SampleType** out, int32 numSamples)
{
    const SampleType yaw   = static_cast<SampleType>(fYaw);
    const SampleType pitch = static_cast<SampleType>(fPitch);
    const SampleType roll  = static_cast<SampleType>(fRoll);

    SampleType* in0  = in[0];   // W
    SampleType* in1  = in[1];   // Y
    SampleType* in2  = in[2];   // Z
    SampleType* in3  = in[3];   // X
    SampleType* out0 = out[0];
    SampleType* out1 = out[1];
    SampleType* out2 = out[2];
    SampleType* out3 = out[3];

    for (int32 i = 0; i < numSamples; ++i)
    {
        rotateYPR (yaw, pitch, roll,
                   in0[i], in1[i], in2[i], in3[i],
                   out0[i], out1[i], out2[i], out3[i]);
    }
}

//─────────────────────────────────────────────────────────────────────────────
// State — save and restore parameter values
//
// We save the raw normalized values (0–1) as 64-bit doubles.
// The order must match between getState and setState.
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API XYPRrotProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    double saved[3];
    if (state->read (saved, sizeof (saved)) != kResultOk)
        return kResultFalse;

    // Restore: saved values are normalized (0–1)
    fYaw   = (-180.0 + saved[0] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;

    // Sync the UI controls
    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[2]);

    return kResultOk;
}

tresult PLUGIN_API XYPRrotProcessor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    // Save normalized values (0–1)
    double saved[3];
    saved[0] = parameters.getParameter (kParamYaw)   ? parameters.getParameter (kParamYaw)->getNormalized ()   : 0.5;
    saved[1] = parameters.getParameter (kParamPitch) ? parameters.getParameter (kParamPitch)->getNormalized () : 0.5;
    saved[2] = parameters.getParameter (kParamRoll)  ? parameters.getParameter (kParamRoll)->getNormalized ()  : 0.5;

    state->write (saved, sizeof (saved));
    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// Bus arrangement — first-order AmbiX only (4 channels)
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API XYPRrotProcessor::setBusArrangements (
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 &&
        SpeakerArr::getChannelCount (inputs[0])  == 4 &&
        SpeakerArr::getChannelCount (outputs[0]) == 4)
    {
        return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// Sample size support
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API XYPRrotProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI — create VSTGUI editor from .uidesc
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API XYPRrotProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "xyprrot.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::XYPRrotProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM XYPRrot",
        0,
        "Fx|Spatial",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::XYPRrotProcessor::createInstance)

END_FACTORY
