//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// The generator of SSCDO#2: on each of the four drivers of STONED, two
// narrow bands of noise that can beat at a distance delta.
//
// FAUST REFERENCE (seam.tedesco.lib):
//
//   lmoband(f)  = fi.highpass(24, f) : fi.lowpass(24, f - 0.0001);
//   lmodens     = sqrt(ma.SR/96000);
//   lmonoise(N) = sno.multinoiseblock(3*N, 0, 2*N);   // blocks 1-2 of 3
//   lmo(N,f,d)  = lmonoise(N)
//               : par(i, N, lmoband(max(1, f - d/2 + i))),
//                 par(i, N, lmoband(f + d/2 + i))
//               :> par(i, N, /(sqrt(2)) : *(lmodens));
//
// Re-implemented by hand (seam-ltm convention) in lmo_dsp.h on three
// reusable headers: seam_noise.h (no.multinoise and sno.multinoiseblock bit for bit),
// seam_butterworth.h (Smith's SVF Butterworth sections), seam_ramp.h.
//
// SR rule of the SSCDO#2 port: it sounds as at 96 kHz at any rate.
// Frequencies are designed at the session's rate, ramps are in seconds,
// and lmodens holds each band at its 96 kHz level.
//
// What the plugin adds to the spec: the glissando of the cues (f moves
// linearly in Hz over `glide` seconds, Pd line semantics; glide 0 = 25 ms,
// which smooths host automation), 25 ms ramps on delta, volume and POWER,
// and a read-only "f now".
//
// Studies and decisions: doc/study/sscdo2/ (lmo-*), logs/2026-10-01-sscdo2-plugins.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "lmo_params.h"

namespace Seam {

class LmoProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    LmoProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new LmoProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* in, Steinberg::int32 numIn,
        Steinberg::Vst::SpeakerArrangement* out, Steinberg::int32 numOut) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }

    lmo::Engine engine_;
    // The controls between the threads: process() and setState() store
    // here; the SDK's Parameter objects are never written from process().
    lmo::ParamBox box_;
};

} // namespace Seam
