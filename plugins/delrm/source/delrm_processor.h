//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// delRM (delRM_duet): four channels, each processing only its own input
// from the TETRAREC. Channels 1 and 3 are a feed-forward comb, x + x[n-D];
// channels 2 and 4 multiply x[n-D], x and its integral, and pass the product
// through an 11:1 compressor that works as a limiter near -20 dBFS.
//
// FAUST REFERENCE (seam.tedesco.lib, seam.filters.lib, seam.math.lib,
// compressors.lib):
//
//   imt2npsamp(mt) = select2(n < 2, n : sff.np, n)
//                    with { mm = floor(mt*1000 + 0.5)/1000;
//                           n  = int(floor(mm*ma.SR/isos + 0.5)); };  isos = 331.4
//   leakyint(fc)   = /(ma.SR) : fi.pole(exp(-2*ma.PI*fc/ma.SR));
//   delrmcomb(mt)  = fi.ff_comb(1 << 15, sma.imt2npsamp(mt), 1, 1);
//   delrmint       = sfi.leakyint(1) : *(96000);
//   delrmrm(mt)    = _ <: de.delay(1 << 15, sma.imt2npsamp(mt)), _, delrmint : *, _ : *;
//   delrmdyn       = *(10) : co.compressor_mono(11, -24, 0.03, 0.04);
//   process        = delrmcomb(mt), (delrmrm(mt) : delrmdyn),
//                    delrmcomb(mt), (delrmrm(mt) : delrmdyn);
//
// Re-implemented by hand (seam-ltm convention) in delrm_dsp.h, on the
// reusable libraries of _common: seam_delays.h (de.delay), seam_filters.h
// (sfi.leakyint), seam_compressors.h (co.compressor_mono), seam_primes.h
// (sma.imt2npsamp through a sieve) and seam_ramp.h. The delay lines are
// sized exactly for 30 m at the session's rate: the spec's 1 << 15 holds
// 30 m up to 192 kHz, not at 384 kHz.
//
// SR rule of the SSCDO#2 port: D is a distance, so a time at every rate,
// with each rate's prime; the integrator is anchored at 96 kHz; the
// compressor is in seconds. delRM sounds at 48 kHz as at 96 kHz.
//
// Against the original (decided with Davide, 2026-10-02): no DC blockers
// (the rehearsal's listening judges the thinner bass), one output fader
// (CC82) in place of Pd's gain and the .dsp's internal 0.9, POWER, 25 ms
// ramps, the input meters and the gain reduction of channels 2 and 4.
//
// Studies and decisions: doc/study/sscdo2/ (delrm-*), logs/2026-10-02-sscdo2-delrm.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "delrm_params.h"

namespace Seam {

class DelrmProcessor : public Steinberg::Vst::SingleComponentEffect,
                       public VSTGUI::VST3EditorDelegate {
public:
    DelrmProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new DelrmProcessor);
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

    // VST3EditorDelegate: the footer line with D.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }
    void publishMeters(Steinberg::Vst::ProcessData& data);

    delrm::Engine   engine_;
    delrm::ParamBox box_;
};

} // namespace Seam
