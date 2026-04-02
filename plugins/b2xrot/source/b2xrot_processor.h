//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · B2Xrot — B-format to AmbiX with rotation
//
// THEORY
// ──────
// Converts a B-format (FuMa) soundfield to AmbiX (ACN/SN3D) and applies
// 3D rotation.
//
// B-format uses FuMa channel ordering and weighting: W, X, Y, Z
//   W is attenuated by 1/√2 (the Furse-Malham convention)
//
// AmbiX uses ACN ordering and SN3D normalization: W, Y, Z, X
//   W has unit gain for a unit-amplitude plane wave
//
// The conversion (btox) scales W by √2 and reorders channels:
//   a0 = W × √2,  a1 = Y,  a2 = Z,  a3 = X
//
// Then rotateYPR applies Roll → Pitch → Yaw rotation.
//
// FAUST REFERENCE (test2/B2Xrot.dsp):
//   process = rotateYPR(yaw,pitch,roll) : btox;
//
// Note: in the Faust code, rotation is applied BEFORE btox because
// Faust's rotateYPR operates on whichever 4-channel signal arrives.
// In our C++ version we do btox first, then rotate — the result is
// equivalent because btox only scales W and reorders, while rotation
// leaves W unchanged.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"

namespace Seam {

class B2XrotProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    B2XrotProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new B2XrotProcessor);
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
    template <typename SampleType>
    void processConvertAndRotate (SampleType** in, SampleType** out, Steinberg::int32 numSamples);

    // Current parameter values (in radians)
    double fYaw   = 0.0;
    double fPitch = 0.0;
    double fRoll  = 0.0;
};

} // namespace Seam
