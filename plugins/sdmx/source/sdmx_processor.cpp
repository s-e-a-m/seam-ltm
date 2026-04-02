//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · SDMX — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "sdmx_processor.h"
#include "sdmx_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include "pluginterfaces/base/ibstream.h"

#include <cstring>

// √2 normalization factor — preserves signal energy across the matrix.
// This is the same 1/√2 used in seam.stereophony.lib: nsum and ndif.
static constexpr double kInvSqrt2 = 0.7071067811865475; // 1.0 / √2

namespace Seam {

using namespace Steinberg;
using namespace Steinberg::Vst;

//─────────────────────────────────────────────────────────────────────────────
// Construction
//─────────────────────────────────────────────────────────────────────────────

SDMXProcessor::SDMXProcessor ()
{
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization — declare buses
//
// No user parameters. The matrix is involutory (A = A⁻¹): the same
// operation encodes LR→MS and decodes MS→LR. A "mode" toggle would
// have no audible effect. Bypass is handled by the host.
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API SDMXProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // Stereo in, stereo out — the matrix is a 2×2 operation
    addAudioInput  (STR16 ("Stereo In"),  SpeakerArr::kStereo);
    addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

    return kResultOk;
}

tresult PLUGIN_API SDMXProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API SDMXProcessor::process (ProcessData& data)
{
    // Guard: nothing to process
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

    // We need exactly 2 channels for the MS matrix.
    // If the host sends mono or surround, just pass through.
    if (numChannels < 2)
    {
        if (in[0] != out[0])
            memcpy (out[0], in[0], sampleFramesSize);
        return kResultOk;
    }

    // Apply the matrix
    if (data.symbolicSampleSize == kSample32)
        processMatrix<Sample32> (reinterpret_cast<Sample32**>(in),
                                 reinterpret_cast<Sample32**>(out),
                                 data.numSamples);
    else
        processMatrix<Sample64> (reinterpret_cast<Sample64**>(in),
                                 reinterpret_cast<Sample64**>(out),
                                 data.numSamples);

    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// DSP core — the Blumlein sum-and-difference matrix
//
// The operation is its own inverse (involutory matrix):
//
//   out0 = (in0 + in1) / √2     — sum (Mid)
//   out1 = (in0 - in1) / √2     — difference (Side)
//
// Apply once: LR → MS. Apply again: MS → LR.
// This is sdmx(x,y) = nsum(x,y), ndif(x,y) from seam.stereophony.lib.
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void SDMXProcessor::processMatrix (SampleType** in, SampleType** out, int32 numSamples)
{
    const SampleType norm = static_cast<SampleType>(kInvSqrt2);

    SampleType* inL  = in[0];
    SampleType* inR  = in[1];
    SampleType* outA = out[0];
    SampleType* outB = out[1];

    for (int32 i = 0; i < numSamples; ++i)
    {
        SampleType a = inL[i];
        SampleType b = inR[i];

        outA[i] = (a + b) * norm;
        outB[i] = (a - b) * norm;
    }
}

//─────────────────────────────────────────────────────────────────────────────
// State — nothing to save (no parameters)
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API SDMXProcessor::setState (IBStream* /*state*/)
{
    return kResultOk;
}

tresult PLUGIN_API SDMXProcessor::getState (IBStream* /*state*/)
{
    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// Bus arrangement — stereo only
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API SDMXProcessor::setBusArrangements (
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 &&
        SpeakerArr::getChannelCount (inputs[0])  == 2 &&
        SpeakerArr::getChannelCount (outputs[0]) == 2)
    {
        return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// Sample size support
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API SDMXProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API SDMXProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "sdmx.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::SDMXProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM SDMX",
        0,
        "Fx|Tools",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::SDMXProcessor::createInstance)

END_FACTORY
