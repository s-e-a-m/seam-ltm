//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// The choir (pitchDetectorChoirMcAdams, four instances): on each channel 16
// bands listen to the TETRAREC A around f*k and make 16 voices of noise sing
// around f*k^a, each as loud as its band of the input.
//
// FAUST REFERENCE (seam.tedesco.lib, seam.noises.lib, filters.lib,
// analyzers.lib):
//
//   choirnoise(N)  = sno.multinoiseblock(N*(2 + 16), 2*N, N*16);
//   choirdens      = sqrt(ma.SR/96000);
//   choirband(fc,q) = fi.svf.bp(min(fc, 19999), q) : *(fc < 20000);
//   choirchan(f,a,q,rel) = (listen, sing) : ro.interleave(16, 2)
//                        : par(k, 16, *) :> /(q*2*ma.PI)
//   with { listen = _ <: par(k, 16, choirband(f*(k+1), q) : an.amp_follower(rel));
//          sing   = par(k, 16, choirband(f*pow(k+1, a), q) : *(choirdens)); };
//   choir(q,rel)   = si.bus(4), choirnoise(4) : (route) :
//                    par(c, 4, choirchan(choirf(c), choira(c), q, rel));
//   choirf = 48, 48, 96, 96;  choira = 1, 1.01, 1.1, 0.9;  q = 350;  rel = 1.5
//
// Re-implemented by hand (seam-ltm convention) in choir_dsp.h on the
// reusable libraries of _common: seam_svf.h (fi.svf.bp), seam_analyzers.h
// (an.amp_follower), seam_basics.h (ba.tau2pole), seam_noise.h
// (sno.multinoiseblock), seam_ramp.h, seam_denormals.h.
//
// SR rule of the SSCDO#2 port: centres in Hz, follower in seconds, voices
// at their 96 kHz level (choirdens): the choir sounds at 48 kHz as at 96.
//
// Against the original (Giuseppe, 2026-10-02): no DC blocker, voices at
// their 96 kHz level, bands from 20 kHz silent, the noise is block 3 of the
// SSCDO#2 noise (one stream per band, decorrelated from LMO and between
// channels); f, a, Q and the release are the performance's constants,
// shown in the window; POWER, output (CC86), RESET, the 4x16 grid.
//
// Studies and decisions: doc/study/sscdo2/ (choir-*), logs/2026-10-02-sscdo2-choir.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "choir_params.h"

namespace Seam {

class ChoirProcessor : public Steinberg::Vst::SingleComponentEffect,
                       public VSTGUI::VST3EditorDelegate {
public:
    ChoirProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new ChoirProcessor);
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

    // VST3EditorDelegate: the grid and the RESET square.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }

    choir::Engine   engine_;
    choir::ParamBox box_;
};

} // namespace Seam
