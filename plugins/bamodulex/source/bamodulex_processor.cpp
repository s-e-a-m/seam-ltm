//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BAMODULEX — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "bamodulex_processor.h"
#include "bamodulex_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include "pluginterfaces/base/ibstream.h"

#include <cstring>

// 1/2 normalization factor — the orthogonal AmbiX→tetrahedral matrix
// scales every output by 1/2 so that abmodulex ∘ bamodulex = identity.
static constexpr double kHalf = 0.5;

namespace Seam {

using namespace Steinberg;
using namespace Steinberg::Vst;

//─────────────────────────────────────────────────────────────────────────────
// Construction
//─────────────────────────────────────────────────────────────────────────────

BAMODULEXProcessor::BAMODULEXProcessor ()
{
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization — declare buses
//
// 4 channels in (AmbiX a0,a1,a2,a3) → 4 channels out (LFU, RFD, RBU, LBD).
// No user parameters: the matrix is a fixed orthogonal decoder.
// VST3 has no native "tetrahedron" speaker arrangement, so the output
// is declared as kAmbi1stOrderACN (4ch) and the host routes channels
// to the four physical loudspeakers.
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API BAMODULEXProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    addAudioInput  (STR16 ("AmbiX In"),         SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput (STR16 ("Tetrahedral Out"),  SpeakerArr::kAmbi1stOrderACN);

    return kResultOk;
}

tresult PLUGIN_API BAMODULEXProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API BAMODULEXProcessor::process (ProcessData& data)
{
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

    if (inChannels < 4 || outChannels < 4)
    {
        for (int32 ch = 0; ch < outChannels; ++ch)
            memset (out[ch], 0, sampleFramesSize);
        return kResultOk;
    }

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
// DSP core — Gerzon BA-module (AmbiX variant)
//
//   LFU = (a0 + a1 + a2 + a3) / 2
//   RFD = (a0 - a1 - a2 + a3) / 2
//   RBU = (a0 - a1 + a2 - a3) / 2
//   LBD = (a0 + a1 - a2 - a3) / 2
//
// AmbiX (ACN): a0=W, a1=Y, a2=Z, a3=X.
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void BAMODULEXProcessor::processMatrix (SampleType** in, SampleType** out, int32 numSamples)
{
    const SampleType half = static_cast<SampleType>(kHalf);

    SampleType* a0 = in[0];
    SampleType* a1 = in[1];
    SampleType* a2 = in[2];
    SampleType* a3 = in[3];

    SampleType* lfu = out[0];
    SampleType* rfd = out[1];
    SampleType* rbu = out[2];
    SampleType* lbd = out[3];

    for (int32 i = 0; i < numSamples; ++i)
    {
        SampleType w = a0[i];
        SampleType y = a1[i];
        SampleType z = a2[i];
        SampleType x = a3[i];

        lfu[i] = (w + y + z + x) * half;
        rfd[i] = (w - y - z + x) * half;
        rbu[i] = (w - y + z - x) * half;
        lbd[i] = (w + y - z - x) * half;
    }
}

//─────────────────────────────────────────────────────────────────────────────
// State — nothing to save (no parameters)
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API BAMODULEXProcessor::setState (IBStream* /*state*/)
{
    return kResultOk;
}

tresult PLUGIN_API BAMODULEXProcessor::getState (IBStream* /*state*/)
{
    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// Bus arrangement — 4 in, 4 out
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API BAMODULEXProcessor::setBusArrangements (
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

tresult PLUGIN_API BAMODULEXProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API BAMODULEXProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "bamodulex.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::BAMODULEXProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM BAMODULEX",
        0,
        "Fx|Spatial",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::BAMODULEXProcessor::createInstance)

END_FACTORY
