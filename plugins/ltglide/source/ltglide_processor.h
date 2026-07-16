#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "ltglide_ids.h"
#include "ltglide_dsp.h"
#include "seam_calbus_client.h"

#include <atomic>

// FAUST REFERENCE (seam.linkwitz.lib):
//
//   sweepfreq(f0,f1,smode,p) = select2(smode, f0+(f1-f0)*p, f0*pow(f1/f0,p));
//   glissburst(N,delta,dmode,fsig) = sin(2*ma.PI*u)*win with {
//       grain = loop ~ (_,_) with { ... onset -> latch fsig ... };
//       u = fg*phase*Tg; win = (u<N)*(0.5-0.5*cos(2*ma.PI*u/N)); };  // N=5
//
// This plugin re-implements the glissando generator by hand in C++ (project
// convention — seam-ltm/CLAUDE.md). The DSP lives in the SDK-free header
// ltglide_dsp.h (SweepFreq + GlissBurst + GlideTransport); this processor owns
// the progress p, feeds SweepFreq(p) into GlissBurst, applies the dBFS Level
// gain, and emits head/tail Dirac markers, over a mono output bus.

namespace Seam {

class LTGLIDEProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    LTGLIDEProcessor();
    ~LTGLIDEProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new LTGLIDEProcessor();
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
    std::atomic<double> paramLevelDb_{kLevelDefaultDb};
    std::atomic<double> paramF0Hz_{kF0DefaultHz};
    std::atomic<double> paramF1Hz_{kF1DefaultHz};
    std::atomic<int>    paramSmode_{1};                 // exponential
    std::atomic<int>    paramDmode_{1};                 // gap
    std::atomic<double> paramDeltaSec_{kDeltaDefaultSec};
    std::atomic<double> paramTSec_{kTDefaultSec};
    std::atomic<bool>   paramLoop_{false};
    std::atomic<int>    paramStoneId_{0};      // 0 = undeclared, 1..8

    // Calibration bus (Spec 2).
    int32_t  busHandle_ = SEAM_CALBUS_NO_HANDLE;
    // Publish-policy bookkeeping (pass-start detect, running/idle edge
    // detect, anchor latch) -- extracted to the SDK-free ltglide::BusAnchor
    // so it is unit-tested (tests/ltglide_dsp_test.cpp) without depending on
    // the VST3 SDK. See ltglide_dsp.h for the -1 contract this exists for.
    ltglide::BusAnchor busAnchor_;

    // Publish this instance's record. Called from the audio thread.
    // hostStartSample is -1 when the host provides no valid continuous clock.
    void publishBusRecord(int64_t hostStartSample);

    // DSP.
    ltglide::GlissBurst    glide_;
    ltglide::GlideTransport transport_;
    double prevGainLin_ = 0.0;

    double computeGainLin() const;
    void   readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples,
                      int64_t blockStartSample);
};

} // namespace Seam
