//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · B2Xrot — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "b2xrot_processor.h"
#include "b2xrot_ids.h"
#include "version.h"
#include "seam_btox.h"
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

B2XrotProcessor::B2XrotProcessor ()
{
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization — declare buses and parameters
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API B2XrotProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // 4 channels in (B-format), 4 channels out (AmbiX)
    // We use kAmbi1stOrderACN for both — the host sees 4-channel ambisonics.
    // The user is responsible for feeding B-format into the input.
    addAudioInput  (STR16 ("B-format In"), SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput (STR16 ("AmbiX Out"),   SpeakerArr::kAmbi1stOrderACN);

    // Parameters: rotation angles in degrees, -180 to +180, default 0
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

tresult PLUGIN_API B2XrotProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API B2XrotProcessor::process (ProcessData& data)
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

    if (numChannels < 4)
    {
        for (int32 ch = 0; ch < numChannels; ++ch)
            if (in[ch] != out[ch])
                memcpy (out[ch], in[ch], sampleFramesSize);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32)
        processConvertAndRotate<Sample32> (reinterpret_cast<Sample32**>(in),
                                           reinterpret_cast<Sample32**>(out),
                                           data.numSamples);
    else
        processConvertAndRotate<Sample64> (reinterpret_cast<Sample64**>(in),
                                           reinterpret_cast<Sample64**>(out),
                                           data.numSamples);

    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// DSP core — B-format to AmbiX conversion, then rotation
//
// Step 1: bFormatToAmbiX  — W×√2, reorder (W,X,Y,Z) → (W,Y,Z,X)
// Step 2: rotateYPR       — Roll → Pitch → Yaw on the AmbiX channels
//
// Note: the Faust reference (B2Xrot.dsp) applies rotateYPR before btox.
// Since rotateYPR assumes ACN channel order (W,Y,Z,X), applying it to
// FuMa-ordered channels (W,X,Y,Z) would rotate the wrong axes.
// Our C++ version converts to AmbiX first, ensuring correct rotation.
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void B2XrotProcessor::processConvertAndRotate (SampleType** in, SampleType** out, int32 numSamples)
{
    const SampleType yaw   = static_cast<SampleType>(fYaw);
    const SampleType pitch = static_cast<SampleType>(fPitch);
    const SampleType roll  = static_cast<SampleType>(fRoll);

    // Input: B-format (FuMa order: W, X, Y, Z)
    SampleType* inW = in[0];
    SampleType* inX = in[1];
    SampleType* inY = in[2];
    SampleType* inZ = in[3];

    SampleType* out0 = out[0];
    SampleType* out1 = out[1];
    SampleType* out2 = out[2];
    SampleType* out3 = out[3];

    for (int32 i = 0; i < numSamples; ++i)
    {
        // Step 1: B-format → AmbiX
        SampleType a0, a1, a2, a3;
        bFormatToAmbiX (inW[i], inX[i], inY[i], inZ[i],
                        a0, a1, a2, a3);

        // Step 2: Rotate in AmbiX domain
        rotateYPR (yaw, pitch, roll,
                   a0, a1, a2, a3,
                   out0[i], out1[i], out2[i], out3[i]);
    }
}

//─────────────────────────────────────────────────────────────────────────────
// State — save and restore
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API B2XrotProcessor::setState (IBStream* state)
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

tresult PLUGIN_API B2XrotProcessor::getState (IBStream* state)
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
// Bus arrangement — 4 channels only
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API B2XrotProcessor::setBusArrangements (
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

tresult PLUGIN_API B2XrotProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API B2XrotProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "b2xrot.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::B2XrotProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM B2Xrot",
        0,
        "Fx|Spatial",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::B2XrotProcessor::createInstance)

END_FACTORY
