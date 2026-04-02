//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · SDMX — Sum and Difference Matrix
//
// THEORY (Alan Blumlein, 1933)
// ────────────────────────────
// The sum-and-difference (Mid/Side) matrix converts between two
// representations of a stereo signal:
//
//   LR domain:  Left and Right channels as picked up by a stereo pair
//   MS domain:  Mid (sum) and Side (difference) components
//
// The transformation:
//   M = (L + R) / √2
//   S = (L - R) / √2
//
// The √2 normalization preserves signal energy (power-invariant).
//
// This matrix is involutory — it is its own inverse (A = A⁻¹).
// Apply once: LR → MS. Apply again: MS → LR.
// No "mode" parameter is needed.
//
//        ┌             ┐
//        │ 1/√2  1/√2  │
//   A =  │             │ = A⁻¹
//        │ 1/√2 -1/√2  │
//        └             ┘
//
// FAUST REFERENCE (seam.stereophony.lib):
//   nsum(x,y) = (x+y)/sqrt(2);
//   ndif(x,y) = (x-y)/sqrt(2);
//   sdmx(x,y) = nsum(x,y), ndif(x,y);
//
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"

namespace Seam {

class SDMXProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    SDMXProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new SDMXProcessor);
    }

    // --- IComponent ---
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements (
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;

    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    // The matrix, templated for 32-bit and 64-bit processing.
    template <typename SampleType>
    void processMatrix (SampleType** in, SampleType** out, Steinberg::int32 numSamples);
};

} // namespace Seam
