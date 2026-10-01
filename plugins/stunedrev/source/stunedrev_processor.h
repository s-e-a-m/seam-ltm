//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// The APF of the score: four independent lines of 42 all-pass sections in
// series, one per face of STONED, each tuned by an irrational ratio. A long
// memory more than a reverberation: the energy of a line returns on average
// after the sum of its 42 delays (106, 69, 17, 201 s at the starting times).
//
// FAUST REFERENCE (seam.tedesco.lib, seam.moorer.lib, seam.math.lib):
//
//   apfv(md,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(md,t-1),_)~(0-_) : mem+_;
//   ms2npsamp(ms)  = select2(n < 2, n : sff.np, n)
//                    with { n = int(floor(ms*ma.SR/1000 + 0.5)); };
//   stdel(k,i,ms)  = sma.ms2npsamp(ms*(i+1)*k);
//   stmd(k,i)      = int(100*(i+1)*k*ma.SR/1000) + 150;
//   stline(k,ms)   = seq(i, 42, sjm.apfv(stmd(k,i), stdel(k,i,ms), 1/sqrt(2)));
//   stunedrev(t1,t2,t3,t4) = stline(sqrt(2),t1), stline((1+sqrt(5))/2,t2),
//                            stline(ma.E,t3), stline(ma.PI,t4);
//
// Re-implemented by hand (seam-ltm convention) in stunedrev_dsp.h, on the
// reusable libraries of _common: seam_moorer.h (sjm.apfv), seam_primes.h (a
// sieve: sff.np without division on the audio thread) and seam_ramp.h. Each section is sized exactly for its longest delay in one
// arena allocated in setActive: stmd's +150 is Faust's compile-time margin.
//
// SR rule of the SSCDO#2 port: times are milliseconds at the session's
// rate, so every memory is the 96 kHz one; each rate has its own primes.
//
// What the plugin adds to the spec: input and output gains (CC83, CC84),
// POWER, 25 ms ramps, RESET, and the centroid readout.
//
// Studies and decisions: doc/study/sscdo2/ (stunedrev-*), logs/2026-10-01-sscdo2-plugins.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "stunedrev_params.h"

namespace Seam {

class StunedrevProcessor : public Steinberg::Vst::SingleComponentEffect,
                           public VSTGUI::VST3EditorDelegate {
public:
    StunedrevProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new StunedrevProcessor);
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

    // VST3EditorDelegate: the RESET square and the footer.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }

    stunedrev::Engine   engine_;
    stunedrev::ParamBox box_;
};

} // namespace Seam
