//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · DDELAY — Quad Speaker-Alignment Delay
//
// PURPOSE
// ───────
// A 4-channel pure-integer sample delay for time-aligning loudspeakers
// in a sound-reinforcement chain. The user enters the distance in
// metres; the plugin converts to samples using the interior speed of
// sound (c = 331.4 m/s, matching `seam.math.lib::isos`) and rounds the
// result up to the next prime number.
//
// All four channels share the same delay value (synchronous).
// Per-speaker incommensurability is achieved across multiple plugin
// instances by the prime quantization — the same principle that
// staggers Schroeder allpass delay lengths in artificial reverbs,
// transposed here to acoustic loudspeaker pathing.
//
// The delay is integer-sample only: no fractional interpolation, no
// crossfade, no smoothing. This preserves the attack transient
// faithfully at the cost of a click on parameter change. Set the
// distance once, leave it.
//
// FAUST REFERENCE (seam.math.lib):
//   isos = 331.4;
//   imt2samp(mt) = int(mt*ma.SR/isos);
//
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"

#include <vector>
#include <atomic>

namespace Seam {

class DDELAYProcessor : public Steinberg::Vst::SingleComponentEffect
{
public:
    DDELAYProcessor ();

    static Steinberg::FUnknown* createInstance (void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new DDELAYProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements (
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;

    Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    static constexpr int kNumChannels = 4;
    static constexpr double kSpeedOfSound = 331.4;     // m/s, interior
    static constexpr double kMaxDistance  = 30.0;      // m

    // Ring buffer sized to a power of two for cheap index masking.
    // Max samples needed at 192 kHz: 30 m * 192000 / 331.4 ≈ 17386.
    // Add headroom for nextprime overshoot and some slack: 32768.
    static constexpr int kBufferSize = 32768;
    static constexpr int kBufferMask = kBufferSize - 1;

    std::vector<float>  buffer32_[kNumChannels];
    std::vector<double> buffer64_[kNumChannels];
    int writeIndex_ = 0;

    // Distance in metres as set by the user (normalized 0–1 in
    // VST3 parameter space mapped to 0–kMaxDistance metres).
    double distanceMeters_ = 0.0;

    // Delay length in samples after metres→samples conversion and
    // nextprime quantization. Read by the audio thread; written when
    // the user changes the slider. Atomic for memory ordering.
    std::atomic<int> delaySamples_ { 0 };

    template <typename SampleType>
    void processDelay (SampleType** in, SampleType** out,
                       std::vector<SampleType> (&buf)[kNumChannels],
                       Steinberg::int32 numSamples);

    // Recompute delaySamples_ from distanceMeters_ and current SR.
    void updateDelaySamples ();
};

} // namespace Seam
