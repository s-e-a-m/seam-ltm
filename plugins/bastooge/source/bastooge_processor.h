//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · BASTOOGE — S.T.OOGE mk2g decoder
//
// FAUST REFERENCE (seam.st.lib): stt.bastooge(fc, test, bypass)
//
//   bastooge(fc, test, bypass) = selector(test) : W bell + all-pass(fc),
//                                dipoles LR4 high-pass(fc) : sam.bamodulex
//   (the full expression is quoted in bastooge_dsp.h)
//
// A first-order AmbiX (ACN: W, Y, Z, X) decoder to the four faces of the
// S.T.OOGE body (LFU, RFD, RBU, LBD), with the BAMODULEX matrix and, in
// front of it, the filters that make the body's dipoles and its W agree:
//
//   - the dipoles Y, Z, X go through a Linkwitz-Riley high-pass of 4th order
//     at the cut fc (100-250 Hz, default 160 Hz), so the cones do not take
//     the bass unloaded;
//   - W goes through an all-pass at fc, which gives it the phase of the
//     dipole high-pass, and through a bell fitted to the body, which matches
//     W's level to what the dipoles reach above the cut.
//
// Provenance: the S.T.OOGE mk2g measurements of 2026-11-18. The body's
// dipole/W ratio follows the model r = x²/(1+x²), x = f/f1, f1 = 359.3 Hz;
// the bell (f0, Q, gain) as a function of fc is the 16-row table in
// plugins/_common/seam_st.h, interpolated as the Faust does.
// S.T.OOGE is developed with Francesco Palmieri.
//
// Parameters: Cut (logarithmic, 100-250 Hz), Test (Normal, W, X, Y, Z: in a
// test mode input A0 alone feeds that mode with gain 2), Bypass (the filters
// out, the matrix in). Bypass is not POWER: POWER off means silent in the
// suite, and here the matrix keeps sounding.
//
// State: three little-endian doubles [cutHz, test, bypass], append-only
// (bastooge_state.h).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "bastooge_dsp.h"
#include "bastooge_readout_view.h"
#include "bastooge_state.h"

namespace Seam {

class BASTOOGEProcessor : public Steinberg::Vst::SingleComponentEffect,
                          public VSTGUI::VST3EditorDelegate {
public:
    BASTOOGEProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new BASTOOGEProcessor);
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

    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) override;

private:
    void applyParams();   // box_ -> engine_, on the audio thread or at activation

    bastooge::Engine   engine_;
    bastooge::ParamBox box_;
    bastooge::Readout  readout_;
};

} // namespace Seam
