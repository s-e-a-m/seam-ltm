//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · XYPRrot — First-order AmbiX rotation
//
// THEORY
// ──────
// 3D rotation of a first-order AmbiX soundfield (ACN/SN3D, 4 channels).
//
// Channel 0 (W) is omnidirectional and unaffected by rotation.
// Channels 1–3 (Y, Z, X) are rotated by applying three successive
// rotation matrices in intrinsic order: Roll → Pitch → Yaw.
//
//   Roll  = rotation around X-axis (tilts left-right vertical plane)
//   Pitch = rotation around Y-axis (tilts front-back vertical plane)
//   Yaw   = rotation around Z-axis (rotates horizontal plane)
//
// FAUST REFERENCE (test2/XYPRrot.dsp):
//   rotateYPR(yaw, pitch, roll, a0, a1, a2, a3) =
//       a0, a1_ypr, a2_ypr, a3_ypr
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"

namespace Seam {

class XYPRrotProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    XYPRrotProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new XYPRrotProcessor);
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

    // --- IEditController ---
    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    template <typename SampleType>
    void processRotation (SampleType** in, SampleType** out, Steinberg::int32 numSamples);

    // Current parameter values (in radians)
    double fYaw   = 0.0;
    double fPitch = 0.0;
    double fRoll  = 0.0;
};

} // namespace Seam
