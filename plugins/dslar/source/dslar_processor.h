#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "dslar_ids.h"
#include "dslar_dsp.h"

#include <atomic>

// FAUST REFERENCE (seam.discipio.lib): sds.lar — the feedforward mono LAR
// processor of Di Scipio's LAR.pd (the Larsen loop is acoustic, external).
// The DSP lives hand-written in the SDK-free dslar_dsp.h; this processor wires
// it to VST3 parameters and publishes the r/g meters (seam-ltm/CLAUDE.md: Faust
// is the spec, readable C++ is the deliverable).

namespace Seam {

class DSLARProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    DSLARProcessor();
    ~DSLARProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new DSLARProcessor();
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
    std::atomic<double> paramPower_{0.0};
    std::atomic<double> paramDrive_{kDriveDef};
    std::atomic<double> paramTarget_{kTargetDef};
    std::atomic<double> paramSteep_{kSteepDef};
    std::atomic<double> paramSmooth_{kSmoothDef};
    std::atomic<double> paramTab1_{kTab1Def};
    std::atomic<double> paramTab2_{kTab2Def};
    std::atomic<double> paramOutput_{kOutDef};

    dslar::Larsen larsen_;

    void applyParams();
    void readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** in, SampleType** out, int numSamples);
};

} // namespace Seam
