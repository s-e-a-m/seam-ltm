#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "strx_ids.h"
#include "strx_dsp.h"

#include <vector>

// FAUST REFERENCE: see strx_dsp.h — sst.sdmx (seam.stereophony.lib) +
// san.correlation/width/panorama/vectorangle (seam.analyzers.lib). The DSP
// lives hand-written in the SDK-free strx_dsp.h; this processor wires it to
// a stereo pass-through bus (seam-ltm/CLAUDE.md: Faust is the spec, readable
// C++ is the deliverable).
//
// strx is an observation analyzer, not an effect: it has no automatable
// parameters and never alters the audio. `process()` copies input to output
// unchanged and feeds the same samples to Seam::strx::Analyzer, whose frames
// the (not-yet-built) goniometer/spectrum/meter views will read on the GUI
// thread via tryReadFrame().

namespace Seam {

class StrxProcessor : public Steinberg::Vst::SingleComponentEffect,
                      public VSTGUI::VST3EditorDelegate {
public:
    StrxProcessor();
    ~StrxProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new StrxProcessor();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSize) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // VST3EditorDelegate — build the meters/goniometer/spectrum custom views
    // (Tasks 7-9) by their kView* name tags (strx_ids.h).
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) override;

    // The analyzer core. This accessor is for the AUDIO-thread lifecycle
    // (prepare/reset/process) only. GUI custom views MUST read frames via
    // latestFrame() (a single shared cached consumer); never call
    // analyzer().tryReadFrame() directly — the triple-buffer is
    // single-consumer and direct reads would starve the other views.
    Seam::strx::Analyzer& analyzer() { return analyzer_; }

    // GUI-thread-only cached accessor for the latest published AnalysisFrame.
    //
    // The triple-buffer (analyzer_.tryReadFrame) is a single-consumer SPSC
    // reader: it hands back a new frame exactly ONCE per audio-thread publish
    // (the dirty bit is consumed on read). Tasks 7-9 add THREE custom views
    // (meters/goniometer/spectrum), each repainting off its own timer; if each
    // called tryReadFrame() directly, only the first poller each tick would
    // ever see fresh data and the other two would starve. Instead every view
    // calls this single cached accessor: it drains tryReadFrame() into
    // frameCache_ (a no-op when nothing new has published since the last
    // call) and always returns the last-known frame. All view timers fire on
    // the GUI thread, serialized, so there is no concurrent-access hazard on
    // frameCache_ itself.
    const Seam::strx::AnalysisFrame& latestFrame() {
        analyzer_.tryReadFrame(frameCache_);
        return frameCache_;
    }

    // Sample rate passed to setupProcessing/analyzer_.prepare. This is config
    // (fixed at prepare time), not per-frame data, so custom views may read it
    // directly on the GUI thread — unlike AnalysisFrame contents, it needs no
    // triple-buffer. StrxSpectrum (Task 9) uses it for the bin -> Hz mapping.
    double sampleRate() const { return sampleRate_; }

private:
    Seam::strx::Analyzer analyzer_;
    Seam::strx::AnalysisFrame frameCache_;
    double sampleRate_ = 48000.0;

    // Analyzer::process() always takes float buffers. When the host processes
    // in kSample64 (double), the pass-through copy stays double-precision but
    // the analysis path needs a float conversion; these scratch buffers are
    // sized once in setupProcessing() to processSetup.maxSamplesPerBlock so
    // the audio thread never allocates.
    std::vector<float> convL_, convR_;

    template <typename SampleType>
    void processBlock(SampleType** in, SampleType** out, int numSamples);
};

} // namespace Seam
