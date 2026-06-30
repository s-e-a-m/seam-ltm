#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "ltburst_ids.h"
#include "ltburst_dsp.h"

#include <atomic>

// FAUST REFERENCE (seam.linkwitz.lib):
//
//   shapedburst(f0,N,dwell) = sin(2*ma.PI*P*c) * win with {
//       M = max(1, int(ceil(dwell*f0))); P = N + M;
//       c = os.phasor(1, f0/P); u = P*c;
//       win = (u < N) * (0.5 - 0.5*cos(2*ma.PI*u/N)); };
//   shapedburst5(f0,dwell) = shapedburst(f0,5,dwell);   // canonical N=5
//
// This plugin re-implements the fixed-frequency generator by hand in C++
// (project convention — see seam-ltm/CLAUDE.md). The DSP lives in the
// SDK-free header ltburst_dsp.h; this processor wires it to VST3 parameters
// (Reference/Trim level, Frequency, Dwell) and a stereo mono-duplicated bus.

namespace Seam {

class LTBURSTProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    LTBURSTProcessor();
    ~LTBURSTProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new LTBURSTProcessor();
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

private:
    // Parameters (audio-thread-readable).
    std::atomic<int>    paramReferenceIdx_{0};   // 0..2
    std::atomic<double> paramTrimDb_{0.0};       // -6..+6
    std::atomic<double> paramFreqHz_{1000.0};    // 20..20000
    std::atomic<double> paramDwellMs_{300.0};    // 0..1000

    // DSP core + previous block gain (for a per-block linear gain ramp).
    ltburst::ShapedBurst burst_;
    double prevGainLin_ = 0.0;

    double computeGainLin() const;
    void   readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples);
};

} // namespace Seam
