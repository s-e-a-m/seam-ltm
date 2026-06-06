//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · X2UHJ — AmbiX → UHJ C-format
//
// Converts First-Order AmbiX (ACN/SN3D, 4ch) to UHJ C-format (L,R,T,Q).
// In SEAM/E4L usage this is the "UHJ decoder" (decode Ambisonics for
// 2-channel listening); in Gerzon's terms it is the UHJ *encode*.
// Input is AmbiX, output is UHJ C-format.
//
// Σ = 0.9396926·W + 0.1855740·X
// Δ = j(−0.3420201·W + 0.5098604·X) + 0.6554516·Y
// L = (Σ+Δ)/2   R = (Σ−Δ)/2
// T = j(−0.1432·W + 0.6512·X) − 0.7071·Y
// Q = 0.9772·Z
// j = quadrature pair H_R/H_I (≈90° apart). See x2uhj_dsp.h, tools/.
//
// FAUST REFERENCE (seam.ambisonics.lib): x2uhj  (canonical form added by
// this work; the two legacy .dsp drafts disagreed — see design spec).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "x2uhj_dsp.h"

namespace Seam {

class X2UHJProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    X2UHJProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new X2UHJProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* in, Steinberg::int32 numIn,
        Steinberg::Vst::SpeakerArrangement* out, Steinberg::int32 numOut) SMTG_OVERRIDE;

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    template <typename S> void processBlock(S** in, S** out, Steinberg::int32 n);
    x2uhj::UHJEncoder encoder;
};

} // namespace Seam
