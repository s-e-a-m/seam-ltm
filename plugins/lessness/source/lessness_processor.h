//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · lessness — the hub's detector.
//
// No FAUST REFERENCE: lessness has no DSP. Its audio path is a copy, and
// what it is for lives in its window, which shows what passes on libseamhub
// (plugins/_common/hub/README.md): the members and their states, the
// proposals with each receiver's lifecycle, the session sheet's last lines
// and its status. The lines come from lessness_view.h, the views that draw
// them from lessness_panes.h and lessness_buttons.h.
//
// What lessness does NOT do is part of the design:
//   - it never registers on the hub: a detector that joined would appear in
//     what it detects, arrive and leave in the sheet, and change the session
//     it is meant to watch;
//   - its audio thread never touches the hub: every hub call is made from the
//     GUI thread, by the views' timers and the two buttons;
//   - it changes nothing on the hub but the sheet's folder and "start a new
//     sheet", the two things an operator needs from the room.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <cstring>

namespace Seam {
namespace lessness {

// The audio path, whole: every channel out is the same channel in, bit for
// bit. A host that processes in place hands the same pointer on both sides,
// and copying a buffer onto itself is skipped rather than relied on.
// A null input (allowed for a silent channel by some hosts) gives silence.
template <typename T>
inline void passThroughT(T* const* in, T* const* out, int32_t channels, int32_t frames) {
    if (!in || !out || channels <= 0 || frames <= 0) return;
    for (int32_t c = 0; c < channels; ++c) {
        if (!out[c] || in[c] == out[c]) continue;
        if (in[c]) std::memcpy(out[c], in[c], (size_t)frames * sizeof(T));
        else       std::memset(out[c], 0, (size_t)frames * sizeof(T));
    }
}

inline void passThrough(float* const* in, float* const* out, int32_t channels, int32_t frames) {
    passThroughT<float>(in, out, channels, frames);
}

inline void passThrough(double* const* in, double* const* out, int32_t channels, int32_t frames) {
    passThroughT<double>(in, out, channels, frames);
}

// The bus rule: one input and one output of the same width, from mono to
// 64 channels (third-order Ambisonics and beyond), whatever the arrangement
// is called. A detector inserted on a bus must not reshape it.
inline bool acceptsChannels(int32_t inChannels, int32_t outChannels) {
    return inChannels == outChannels && inChannels >= 1 && inChannels <= 64;
}

} // namespace lessness
} // namespace Seam

// The plugin class. tests/lessness_passthrough_test.cpp defines
// SEAM_LESSNESS_PURE to read the two functions above without the SDK.
#ifndef SEAM_LESSNESS_PURE

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "lessness_ids.h"

#include <memory>
#include <string>

namespace Seam {

class LessnessProcessor : public Steinberg::Vst::SingleComponentEffect,
                          public VSTGUI::VST3EditorDelegate {
public:
    LessnessProcessor() = default;
    ~LessnessProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new LessnessProcessor();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSize) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // VST3EditorDelegate — builds the panes and buttons by their kView* tags.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) override;

    // GUI thread only. The folder the operator chose for the next sheet,
    // empty when there is none waiting. FOLDER writes it, the footer reads it
    // and clears it once the sheet is inside it. A shared_ptr because the
    // folder chooser runs asynchronously and its callback may fire after the
    // editor (and the button that opened it) has closed.
    std::shared_ptr<std::string> nextFolder() const { return nextFolder_; }

private:
    std::shared_ptr<std::string> nextFolder_ = std::make_shared<std::string>();
};

} // namespace Seam

#endif // SEAM_LESSNESS_PURE
