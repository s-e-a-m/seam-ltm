//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LR2XHGR — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "lr2xhgr_processor.h"
#include "lr2xhgr_ids.h"
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

LR2XHGRProcessor::LR2XHGRProcessor ()
{
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization — declare buses and parameters
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API LR2XHGRProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // Stereo input, first-order AmbiX output (4 channels)
    addAudioInput  (STR16 ("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput (STR16 ("AmbiX Out"), SpeakerArr::kAmbi1stOrderACN);

    // Divergence: 0° to 180°, default 90° (as in the Faust code)
    // The Faust slider divides by 2 internally: divergence/2 is the half-angle
    parameters.addParameter (
        new RangeParameter (STR16 ("Divergence"), kParamDivergence, STR16 ("\u00B0"),
                            0.0, 180.0, 90.0, 0, ParameterInfo::kCanAutomate));

    // Rotation angles: -180° to +180°, default 0°
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

tresult PLUGIN_API LR2XHGRProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Activation — reset Haar filter state when starting/stopping
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API LR2XHGRProcessor::setActive (TBool state)
{
    if (state)
    {
        haarL.reset ();
        haarR.reset ();
    }
    return SingleComponentEffect::setActive (state);
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API LR2XHGRProcessor::process (ProcessData& data)
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
                    case kParamDivergence:
                        // Normalized 0–1 → plain 0–180°, then halved and converted to radians
                        fDivergence = (value * 180.0 / 2.0) * M_PI / 180.0;
                        break;
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

    int32 inChannels  = data.inputs[0].numChannels;
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

    if (inChannels < 2 || outChannels < 4)
    {
        for (int32 ch = 0; ch < outChannels; ++ch)
            memset (out[ch], 0, sampleFramesSize);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32)
        processHaarStereo<Sample32> (reinterpret_cast<Sample32**>(in),
                                     reinterpret_cast<Sample32**>(out),
                                     data.numSamples);
    else
        processHaarStereo<Sample64> (reinterpret_cast<Sample64**>(in),
                                     reinterpret_cast<Sample64**>(out),
                                     data.numSamples);

    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// DSP core — Stereo → Haar decomposition → divergence → global rotation
//
// For each sample:
//   1. Decompose L and R independently via Haar wavelet (haarmn(1))
//   2. Rotate L-field by +divergence (yaw only), R-field by -divergence
//   3. Sum the two 4-channel fields
//   4. Apply global rotation (Yaw, Pitch, Roll)
//
// Faust reference: lr2xhgr = par(i,2,m2xhgr) :
//     rotateYPR(divergence,pitch,roll),
//     rotateYPR(0-divergence,pitch,roll) :> si.bus(4);
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void LR2XHGRProcessor::processHaarStereo (SampleType** in, SampleType** out, int32 numSamples)
{
    const SampleType divYaw = static_cast<SampleType>(fDivergence);
    const SampleType yaw    = static_cast<SampleType>(fYaw);
    const SampleType pitch  = static_cast<SampleType>(fPitch);
    const SampleType roll   = static_cast<SampleType>(fRoll);
    const SampleType zero   = static_cast<SampleType>(0);

    SampleType* inL  = in[0];
    SampleType* inR  = in[1];
    SampleType* out0 = out[0];
    SampleType* out1 = out[1];
    SampleType* out2 = out[2];
    SampleType* out3 = out[3];

    for (int32 i = 0; i < numSamples; ++i)
    {
        // Step 1: Haar decomposition of each channel
        SampleType la0, la1, la2, la3;
        SampleType ra0, ra1, ra2, ra3;

        // Haar uses double internally — convert sample to double, decompose, convert back
        double dL = static_cast<double>(inL[i]);
        double dR = static_cast<double>(inR[i]);

        double dla0, dla1, dla2, dla3;
        double dra0, dra1, dra2, dra3;
        haarDecompose (haarL, dL, dla0, dla1, dla2, dla3);
        haarDecompose (haarR, dR, dra0, dra1, dra2, dra3);

        la0 = static_cast<SampleType>(dla0);
        la1 = static_cast<SampleType>(dla1);
        la2 = static_cast<SampleType>(dla2);
        la3 = static_cast<SampleType>(dla3);
        ra0 = static_cast<SampleType>(dra0);
        ra1 = static_cast<SampleType>(dra1);
        ra2 = static_cast<SampleType>(dra2);
        ra3 = static_cast<SampleType>(dra3);

        // Step 2: Apply divergence as opposing yaw rotations
        SampleType lOut0, lOut1, lOut2, lOut3;
        SampleType rOut0, rOut1, rOut2, rOut3;
        rotateYPR (divYaw, pitch, roll,
                   la0, la1, la2, la3,
                   lOut0, lOut1, lOut2, lOut3);
        rotateYPR (-divYaw, pitch, roll,
                   ra0, ra1, ra2, ra3,
                   rOut0, rOut1, rOut2, rOut3);

        // Step 3: Sum the two fields
        SampleType s0 = lOut0 + rOut0;
        SampleType s1 = lOut1 + rOut1;
        SampleType s2 = lOut2 + rOut2;
        SampleType s3 = lOut3 + rOut3;

        // Step 4: Apply global rotation
        rotateYPR (yaw, zero, zero,
                   s0, s1, s2, s3,
                   out0[i], out1[i], out2[i], out3[i]);
    }
}

//─────────────────────────────────────────────────────────────────────────────
// State — save and restore
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API LR2XHGRProcessor::setState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    double saved[4];
    if (state->read (saved, sizeof (saved)) != kResultOk)
        return kResultFalse;

    // Divergence: normalized 0–1 maps to plain 0–180°
    fDivergence = (saved[0] * 180.0 / 2.0) * M_PI / 180.0;
    fYaw   = (-180.0 + saved[1] * 360.0) * M_PI / 180.0;
    fPitch = (-180.0 + saved[2] * 360.0) * M_PI / 180.0;
    fRoll  = (-180.0 + saved[3] * 360.0) * M_PI / 180.0;

    if (auto* p = parameters.getParameter (kParamDivergence))
        p->setNormalized (saved[0]);
    if (auto* p = parameters.getParameter (kParamYaw))
        p->setNormalized (saved[1]);
    if (auto* p = parameters.getParameter (kParamPitch))
        p->setNormalized (saved[2]);
    if (auto* p = parameters.getParameter (kParamRoll))
        p->setNormalized (saved[3]);

    return kResultOk;
}

tresult PLUGIN_API LR2XHGRProcessor::getState (IBStream* state)
{
    if (!state)
        return kResultFalse;

    double saved[4];
    saved[0] = parameters.getParameter (kParamDivergence) ? parameters.getParameter (kParamDivergence)->getNormalized () : 0.5;
    saved[1] = parameters.getParameter (kParamYaw)        ? parameters.getParameter (kParamYaw)->getNormalized ()        : 0.5;
    saved[2] = parameters.getParameter (kParamPitch)      ? parameters.getParameter (kParamPitch)->getNormalized ()      : 0.5;
    saved[3] = parameters.getParameter (kParamRoll)       ? parameters.getParameter (kParamRoll)->getNormalized ()       : 0.5;

    state->write (saved, sizeof (saved));
    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// Bus arrangement — stereo in, 4-channel AmbiX out
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API LR2XHGRProcessor::setBusArrangements (
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 &&
        SpeakerArr::getChannelCount (inputs[0])  == 2 &&
        SpeakerArr::getChannelCount (outputs[0]) == 4)
    {
        return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// Sample size support
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API LR2XHGRProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API LR2XHGRProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "lr2xhgr.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::LR2XHGRProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM LR2XHGR",
        0,
        "Fx|Spatial",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::LR2XHGRProcessor::createInstance)

END_FACTORY
