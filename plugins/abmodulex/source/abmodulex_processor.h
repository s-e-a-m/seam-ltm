//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · abmodulex — A-format → AmbiX
//
// Converts a 4-channel A-format tetrahedral microphone signal
// (LFU, RFD, RBU, LBD) into First-Order AmbiX (ACN/SN3D: W, Y, Z, X).
// Front-end of the TETRAREC → x2uhj → L,R chain. Inverse of bamodulex;
// the matrix is involutory (M² = I), so the coefficients are identical.
//
//   a0 = (LFU + RFD + RBU + LBD) / 2   // W
//   a1 = (LFU − RFD − RBU + LBD) / 2   // Y
//   a2 = (LFU − RFD + RBU − LBD) / 2   // Z
//   a3 = (LFU + RFD − RBU − LBD) / 2   // X
//
// Pure matrix — no capsule-compensation filtering.
//
// FAUST REFERENCE (seam.ambisonics.lib): sam.abmodulex
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "abmodulex_dsp.h"

namespace Seam {

class ABMODULEXProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    ABMODULEXProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new ABMODULEXProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
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
};

} // namespace Seam
