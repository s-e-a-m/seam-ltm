//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — golden all-pass memory (canto alla durata)
//
// Four faces of STONE, each fed by one TETRAREC capsule (A-format), each a
// chain of up to 18 Moorer all-pass sections with g = 1/phi. The delays run
// along one golden spiral through the four faces in turn: section i of face
// k lasts d * phi^(i + k/4) / c, moved to the next prime. The first delay is
// the acoustic distance from TETRAREC to STONE, taken on the STONE id's grid
// so that no two STONEs play the same spiral.
//
// FAUST REFERENCE (seam.silvi.lib, seam.moorer.lib, seam.math.lib):
//
//   ipphase(id) = ba.take(max(id,1), (0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875));
//   ipgrid(id, j) = pow(sma.phi(1), (j + ipphase(id))/4);
//   ipdel(d, i, k) = select2(n < 2, n : sff.np, n)
//   with {
//       n = int(floor(ma.SR * d * pow(sma.phi(1), i + k/4) / sma.rsos + 0.5));
//   };
//   ipmd(i, k) = int(ma.SR * ipgrid(8, 14) * pow(sma.phi(1), i + k/4) / sma.rsos) + 512;
//   ipline(N, k, d) = seq(i, N, sjm.apfv(ipmd(i, k), ipdel(d, i, k), sma.rphi(1)));
//   inphirev(N, id, j) = par(k, 4, ipline(N, k, ipgrid(id, j)));
//
// Re-implemented by hand (seam-ltm convention) in inphirev_dsp.h, on the
// libraries of _common: seam_moorer.h (sjm.apfv), seam_primes.h (sff.np by
// a sieve built in setActive), seam_basics.h (c at 20 degC), seam_ramp.h.
// One arena of doubles, sized in setActive for the worst case of every id,
// grid point and section count: ipmd's margin is Faust's compile-time size.
//
// What the plugin adds to the spec: POWER (a 25 ms output ramp), RESET
// (a GUI-only button that empties the memory), the 250 ms settle of a new
// setting (one reset at the end of a gesture, with its last value), and the
// footer: the decay each face prints (inphirev_decay.h, an estimate computed
// on the GUI thread), the first and longest delay, the arena, the status.
//
// Design: docs/superpowers/specs/2026-10-27-inphirev-plugin-design.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "inphirev_params.h"

namespace Seam {

class InphirevProcessor : public Steinberg::Vst::SingleComponentEffect,
                          public VSTGUI::VST3EditorDelegate {
public:
    InphirevProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new InphirevProcessor);
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
    // The memory is the piece's second part: a host that suspends processing
    // on silent input must not freeze it when the input closes.
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE { return Steinberg::Vst::kInfiniteTail; }
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    // The distance's text depends on the STONE id: a new id redraws it.
    Steinberg::tresult PLUGIN_API setParamNormalized(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;

    // VST3EditorDelegate: the RESET square and the footer.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }
    void applyBox();   // the box's plain values into the engine

    inphirev::Engine   engine_;
    inphirev::ParamBox box_;
};

} // namespace Seam
