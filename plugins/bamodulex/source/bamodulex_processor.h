//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BAMODULEX — AmbiX → Tetrahedral Decoder
//
// THEORY (Gerzon 1975, AmbiX variant)
// ───────────────────────────────────
// Decodes a first-order AmbiX (ACN/SN3D) signal to four loudspeakers
// placed at the vertices of a tetrahedron inscribed in a cube:
//
//   LFU — Left  Front Up      ( +Y +Z +X )
//   RFD — Right Front Down    ( -Y -Z +X )
//   RBU — Right Back  Up      ( -Y +Z -X )
//   LBD — Left  Back  Down    ( +Y -Z -X )
//
// AmbiX channel order (ACN): a0 = W, a1 = Y, a2 = Z, a3 = X.
//
//   LFU = (a0 + a1 + a2 + a3) / 2
//   RFD = (a0 - a1 - a2 + a3) / 2
//   RBU = (a0 - a1 + a2 - a3) / 2
//   LBD = (a0 + a1 - a2 - a3) / 2
//
// The matrix is orthogonal: it is the AmbiX-domain dual of the
// `abmodulex` encoder (4 corners → AmbiX). Compensation shelving
// filters (Gerzon 1975) are omitted by design — the STONE amplifier
// handles HF/LF correction downstream, so the plugin is a pure matrix.
//
// FAUST REFERENCE (seam.ambisonics.lib):
//   bamodulex(a0,a1,a2,a3) = lfu, rfd, rbu, lbd
//   with {
//     lfu = (a0 + a1 + a2 + a3) / 2;
//     rfd = (a0 - a1 - a2 + a3) / 2;
//     rbu = (a0 - a1 + a2 - a3) / 2;
//     lbd = (a0 + a1 - a2 - a3) / 2;
//   };
//
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"

namespace Seam {

class BAMODULEXProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    BAMODULEXProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new BAMODULEXProcessor);
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
    void processMatrix (SampleType** in, SampleType** out, Steinberg::int32 numSamples);
};

} // namespace Seam
