//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — irrational all-pass memory (canto alla durata)
//
// Four faces of STONE, each fed by one TETRAREC capsule (A-format), each a
// chain of up to 128 Moorer all-pass sections with the gain g of the menu.
// The delays run along one spiral of scale r (phi, sqrt 2, e, pi) through the
// four faces in turn, D sections per factor r: section i of face k lasts
// d * r^((i + k/4)/D) / c, moved to the next prime. The first delay is the
// acoustic distance from TETRAREC to STONE, taken on the STONE id's grid so
// that no two STONEs play the same spiral. With phi, D = 1 and g = 1/phi the
// plugin is INPHIREV, delay for delay.
//
// FAUST REFERENCE (seam.silvi.lib, seam.moorer.lib, seam.math.lib):
//
//   alphase(id) = ba.take(max(id,1), (0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875));
//   algrid(r, D, id, j) = pow(r, (j + alphase(id))/(4*D));
//   aldel(r, D, d, i, k) = select2(n < 2, n : sff.np, n)
//   with {
//       n = int(floor(ma.SR * d * pow(r, (i + k/4)/D) / sma.rsos + 0.5));
//   };
//   almd(r, D, i, k) = int(192000 * 6 * pow(r, (i + k/4)/D) / sma.rsos + 0.5) + 1024;
//   alline(r, D, g, N, k, d) = seq(i, N, sjm.apfv(almd(r, D, i, k), aldel(r, D, d, i, k), g));
//   alogos(r, D, g, N, id, j) = par(k, 4, alline(r, D, g, N, k, algrid(r, D, id, j)));
//   (sgs.alogos)
//
// Re-implemented by hand (seam-ltm convention) in alogos_dsp.h, on the
// libraries of _common: seam_moorer.h (sjm.apfv), seam_primes.h (sff.np by
// a sieve built in setActive), seam_basics.h (c at 20 degC), seam_ramp.h.
// Where Faust sizes every section for the worst case at compile time (almd),
// the plugin sizes one arena for the setting in use, each section exactly
// its delay + 1 (alogos_arena.h): allocated in setActive, and for a new
// setting by the broker's worker while the output is silent.
//
// What the plugin adds to the spec: the distance in metres snapped to the
// grid, the section count limited to N_max (90 s for the longest delay),
// POWER (a 25 ms output ramp), RESET (a GUI-only button that empties the
// memory), the 250 ms settle of a new setting, and the footer: the decay
// each face prints (alogos_decay.h, an estimate computed on the GUI thread
// with the setting's g), the table's ends, the arena, the status.
//
// Design: docs/superpowers/specs/2026-10-27-alogos-plugin-design.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "alogos_params.h"

namespace Seam {

// The controller's current values of the settings that shape the distance
// and sections texts: the values the window shows, not the audio thread's.
struct SettingsView {
    const Steinberg::Vst::Parameter* stoneId = nullptr;
    const Steinberg::Vst::Parameter* distance = nullptr;
    const Steinberg::Vst::Parameter* scale = nullptr;
    const Steinberg::Vst::Parameter* density = nullptr;

    int    id() const { return stoneId ? alogos::normalizedToStoneId(stoneId->getNormalized()) : 0; }
    int    sc() const { return scale ? alogos::normalizedToScale(scale->getNormalized()) : 0; }
    int    dn() const { return density ? alogos::normalizedToDensity(density->getNormalized()) : 0; }
    double metres() const {
        return distance ? alogos::normalizedToDistance(distance->getNormalized()) : alogos::kDefaultMetres;
    }
};

class AlogosProcessor : public Steinberg::Vst::SingleComponentEffect,
                        public VSTGUI::VST3EditorDelegate {
public:
    AlogosProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new AlogosProcessor);
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

    // The distance and sections texts depend on the other settings: a change
    // of id, scale, density or distance redraws them.
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

    alogos::Engine   engine_;
    alogos::ParamBox box_;
    SettingsView     view_;   // the parameters' pointers, set in initialize
};

} // namespace Seam
