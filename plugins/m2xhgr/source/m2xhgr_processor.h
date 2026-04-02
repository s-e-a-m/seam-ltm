//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · M2XHGR — Mono to AmbiX via Haar wavelet
//
// THEORY
// ──────
// Encodes a mono signal into a first-order AmbiX soundfield using the
// Haar wavelet decomposition (haarmn(1)), then applies 3D rotation.
//
// This is the mono case of LR2XHGR — one channel, no divergence.
// The Haar QMF bank decomposes the mono input into 4 spatial components
// which are then rotated by Yaw, Pitch, Roll.
//
// FAUST REFERENCE (test2/M2XHGR.dsp):
//   m2xhgr = haarmn(1) : ro.cross(4);
//   process = m2xhgr : rotateYPR(yaw, pitch, roll);
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "seam_haar.h"

namespace Seam {

class M2XHGRProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    M2XHGRProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new M2XHGRProcessor);
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
    void processHaarMono (SampleType* in, SampleType** out, Steinberg::int32 numSamples);

    HaarState<double> haar;

    double fYaw   = 0.0;
    double fPitch = 0.0;
    double fRoll  = 0.0;
};

} // namespace Seam
