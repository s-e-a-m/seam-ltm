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
    // Two facts recorded here belong to the plugin rather than to the design:
    //
    // The 0.39 dB by which the old measured constant (26.45) exceeded the
    // same computation for the old filter (26.06 = -(-4.771 + -21.291)) is
    // NOT explained by the LCG: the source's RMS was measured over 5*10^8
    // samples and lands within 0.00001 dB of 1/sqrt(3). The other two
    // suspects (whether the 2026-05-07 render carried a host fader in
    // addition to the plugin's own gain, and whether `sox stat` integrated a
    // silent lead-in into that render's RMS) still want a render to settle.
    // The gap is not folded into the constant, and the constant is not split
    // with it.
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
