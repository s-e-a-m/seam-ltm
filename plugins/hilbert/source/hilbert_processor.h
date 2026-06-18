//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hilbert — wideband quadrature/Hilbert transformer
//
// One mono input → two outputs. Output [0] is the in-phase branch, output [1]
// is the quadrature branch; their phase difference holds −90° across 20 Hz–
// 20 kHz. Both outputs are all-pass-filtered — neither is the dry signal: the
// quadrature relationship is a property of the *pair*, as in the x2uhj encoder's
// internal H_R/H_I networks.
//
// Two topologies, selectable live and designed per sample rate by the shared
// seam_quadrature engine:
//   RBJ        — 3 second-order all-pass biquads per branch
//   Polyphase  — 4 first-order all-pass sections per path (Niemitalo), 1/3 cost
//
// FAUST REFERENCE (seam.filters.lib): the quadrature/Hilbert pair (roadmap).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "hilbert_dsp.h"

namespace Seam {

class HilbertProcessor : public Steinberg::Vst::SingleComponentEffect,
                         public VSTGUI::VST3EditorDelegate {
public:
    HilbertProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new HilbertProcessor);
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
    void applyTopology(double normalized); // map a normalised param value to the DSP
    template <typename S> void processBlock(S** in, S** out, Steinberg::int32 n);

    hilbert::HilbertTransformer dsp;
};

} // namespace Seam
