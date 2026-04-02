//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LR2XHGR — Stereo to AmbiX via Haar wavelet
//
// THEORY
// ──────
// Giuseppe Silvi's method for encoding a stereo signal into a first-order
// AmbiX soundfield using the Haar wavelet decomposition.
//
// Each stereo channel is independently decomposed by haarmn(1) into 4
// spatial components (the Haar QMF bank). The two resulting 4-channel
// fields are then placed in space using a divergence angle:
//
//   Left channel  → Haar → rotateYPR(+divergence/2, pitch, roll)
//   Right channel → Haar → rotateYPR(-divergence/2, pitch, roll)
//
// The two rotated fields are summed to produce the final AmbiX output.
// A global rotation (Yaw, Pitch, Roll) is applied after the merge.
//
// FAUST REFERENCE (test2/LR2XHGR.dsp):
//   m2xhgr = haarmn(1) : ro.cross(4);
//   lr2xhgr = par(i,2,m2xhgr) :
//       rotateYPR(divergence,pitch,roll),
//       rotateYPR(0-divergence,pitch,roll)
//       :> si.bus(4);
//
// Note: the Faust code uses Divergence as the yaw parameter of two
// opposing rotations. Our C++ version separates this more clearly:
// divergence controls the stereo spread, then a global YPR rotation
// orients the entire soundfield.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "seam_haar.h"

namespace Seam {

class LR2XHGRProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    LR2XHGRProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new LR2XHGRProcessor);
    }

    // --- IComponent ---
    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
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
    void processHaarStereo (SampleType** in, SampleType** out, Steinberg::int32 numSamples);

    // Haar wavelet state: one per stereo channel
    HaarState<double> haarL;
    HaarState<double> haarR;

    // Parameters (in radians)
    double fDivergence = 0.0;   // half-angle between L and R
    double fYaw        = 0.0;
    double fPitch      = 0.0;
    double fRoll       = 0.0;
};

} // namespace Seam
