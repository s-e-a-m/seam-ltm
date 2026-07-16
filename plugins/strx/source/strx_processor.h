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

    // The analyzer core, read by the GUI thread via tryReadFrame() once the
    // custom views (Tasks 7-9) exist.
    Seam::strx::Analyzer& analyzer() { return analyzer_; }

private:
    Seam::strx::Analyzer analyzer_;

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
