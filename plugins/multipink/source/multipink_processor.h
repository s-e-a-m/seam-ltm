#pragma once

#include "multipink_pink.h"

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "multipink_ids.h"
#include "seam_calbus_client.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

// FAUST REFERENCE (seam.noises.lib, seam.filters.lib):
//
//   multipink(N,g) = no.multinoise(N) : par(i,N,sfi.pink_filter_mz : *(g));
//
// where no.multinoise(N) is N parallel LCGs whose per-channel seeds are
// dispersed by noise_env(12345) so the resulting streams are statistically
// independent, and sfi.pink_filter_mz / sfi.spectral_tilt_mz are the
// matched-Z pinking filter designed in multipink_pink.h (which carries the
// derivation and the reasoning). Both functions live in seam.filters.lib
// as of 2026-08-19 -- see that library's PINKING FILTER section, which
// cross-references this plugin, tests/multipink_pink_test.cpp, and
// docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md back.
//
// This plugin re-implements the above in hand-written C++ (project
// convention — see seam-ltm/CLAUDE.md). N is fixed at 64 (the shared
// logical pool size). Per-instance gain is applied in C++ after the IIR.
// Seed dispersion uses splitmix64 (see seedLCGs in the .cpp); functionally
// equivalent to Faust's noise_env, though not bit-identical to its stream.

namespace Seam {

class MULTIPINKProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    MULTIPINKProcessor();
    ~MULTIPINKProcessor() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*) new MULTIPINKProcessor();
    }

    // VST3 lifecycle
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
    // Pool size — the logical shared 64-channel pink-noise pool.
    static constexpr int kPoolSize = 64;

    // Faust master seed, must match no.multinoise(N) = noise_env(12345).
    static constexpr uint32_t kFaustSeed = 12345;

    // Calibration lives in PinkDesign now -- kCalibrationOffsetBaseDb and
    // PinkDesign::calibrationOffsetDb(), in multipink_pink.h, which is
    // SDK-free and therefore readable by a test. It used to be a constant
    // here, where nothing in tests/ could include it, and the rate-dependent
    // term it was missing went unnoticed through eight task reviews and a
    // whole-branch review because no test could see the number at all.
    // computeGainLin() reads pinkDesign_.calibrationOffsetDb().
    //
    // Two facts recorded here belong to the plug-in rather than to the design:
    //
    // The 0.39 dB by which the old measured constant (26.45) exceeded the
    // same computation for the old filter (26.06 = -(-4.771 + -21.291)) was
    // SETTLED on 2026-08-19 by two offline renders, measured per channel:
    // -22.997 dBFS at 48 kHz against a predicted -23.000, and -22.721 at
    // 96 kHz against -22.718. Three millidecibels at both rates, with the
    // predicted +0.2825 dB rise between them measuring +0.276. Measure ONE
    // channel: `sox stat` over the 4-channel render averages streams that
    // scatter a few hundredths of a dB over 31 s, and reads -23.024.
    //
    // So the 0.39 dB was in the 2026-05-07 measurement, not in this
    // arithmetic. Its cause is NOT determined: that render's RMS came out
    // low, which a silent lead-in, a channel not at level in a multichannel
    // average, or a fader would each do, and nothing records how it was made.
    // The LCG had already been cleared to 0.00001 dB over 5*10^8 samples.
    //
    // Retroactively, then: with 26.45 the old plug-in emitted 0.39 dB above
    // the level it announced. The bias is common to every band, so it moved
    // no equalisation decision -- only the absolute level of the reference.
    //
    // Against the old filter at equal total RMS the mean band level moves by
    // -1.00 dB. strx reports deviations from the mean of the bands, so no
    // equalisation decision taken before 2026-08-19 changes; only the
    // absolute level of the reference signal does.

    // Active output channel count (set by setBusArrangements).
    int activeChannels_ = 2;

    // Number of channels claimed in the pool (≤ activeChannels_).
    int claimedChannels_ = 0;

    // Starting slot in the pool (0..63), or -1 if not claimed (sentinel).
    int claimedStart_ = -1;

    // Persisted "preferred" start slot from state, used at next setActive(true).
    int preferredStart_ = -1;

    // Allocation status (drives the GUI LED, set in setActive).
    enum class PoolStatus : int { Unclaimed = 0, ClaimedAtPreferred = 1, ClaimedFirstFit = 2, Exhausted = 3 };
    std::atomic<int> poolStatus_{(int)PoolStatus::Unclaimed};

    // Parameters (audio-thread-readable).
    std::atomic<int>    paramReferenceIdx_{0};   // 0..2
    std::atomic<double> paramTrimDb_{0.0};       // -6..+6
    std::atomic<int>    paramPower_{1};          // 1 = sounding (POWER on)
    std::atomic<int>    paramStoneId_{0};        // 0 = undeclared, 1..8

    // Calibration bus (Spec 2). The handle is claimed in setActive and
    // released there; publishing happens from process() on the audio thread,
    // which is why the bus uses a seqlock and not a mutex.
    int32_t busHandle_ = SEAM_CALBUS_NO_HANDLE;

    // Build and publish this instance's record. Safe to call from the audio
    // thread; a no-op when the bus is unavailable or the slot is unclaimed.
    void publishBusRecord();

    // DSP state — full pool always advanced.
    uint32_t lcgState_[kPoolSize] = {};

    // The pinking filter, designed for the running sample rate in
    // setupProcessing. See multipink_pink.h for why it has this shape.
    Seam::multipink::PinkDesign pinkDesign_;

    // Per-stream filter state, SECTION-MAJOR: state[section][stream].
    // Section-major so that the 64 streams belonging to one section are
    // contiguous. Note for a future reader: this layout is a precondition for
    // advancing eight streams with one SIMD instruction, but not sufficient on
    // its own -- the scratch buffer would also have to be transposed to
    // [sample][stream]. No SIMD is written here.
    float pinkState_[Seam::multipink::PinkDesign::kMaxSections][kPoolSize] = {};

    // Scratch buffer for the full pool: shape [kPoolSize][maxBlockSize].
    std::vector<float>  scratch32_;
    std::vector<double> scratch64_;
    int32_t maxBlockSize_ = 0;

    // Helpers
    void seedLCGs();
    void resetPinkFilters();
    double computeGainLin() const;
    void readParameterChanges(Steinberg::Vst::ProcessData& data);

    template <typename SampleType>
    void processBlock(SampleType** outputs, int numChannels, int numSamples,
                      std::vector<SampleType>& scratch);
};

} // namespace Seam
