#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "multipink_ids.h"
#include "seam_calbus_client.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

// FAUST REFERENCE (seam.noises.lib):
//
//   multipink(N,g) = no.multinoise(N) : par(i,N,no.pink_filter : *(g));
//
// where no.pink_filter is (noises.lib:402):
//
//   pink_filter = fi.iir(
//       (0.049922035, -0.095993537, 0.050612699, -0.004408786),
//       (-2.494956002, 2.017265875, -0.522189400));
//
// and no.multinoise(N) is N parallel LCGs whose per-channel seeds are
// dispersed by noise_env(12345) so the resulting streams are statistically
// independent.
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

    // Pink filter coefficients (Paul Kellet, noises.lib:402).
    static constexpr double kPinkB[4] = {
         0.049922035, -0.095993537,  0.050612699, -0.004408786 };
    static constexpr double kPinkA[3] = {
        -2.494956002,  2.017265875, -0.522189400 };

    // Calibration constant — measured 2026-05-07 against a 30 s render at
    // 48 kHz, Reference=-23, Trim=0: sox reported RMS amplitude 0.003370
    // (= -49.45 dBFS RMS). Raw pink RMS at unity gain is therefore
    // -26.45 dBFS, so the offset to make "Reference=-23, Trim=0" land at
    // -23.0 dBFS RMS is +26.45 dB.
    static constexpr double kCalibrationOffsetDb = 26.45;

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
    std::atomic<int>    paramMute_{0};           // 0 or 1
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
    double   pinkX_[kPoolSize][4] = {};   // input history (white noise)
    double   pinkY_[kPoolSize][3] = {};   // output history (pink)

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
