//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the engine (SDK-free)
//
// delRM of SSCDO#2 on four channels, each processing only its own input.
// Channels 1 and 3: x + x[n-D] (sdt.delrmcomb). Channels 2 and 4: x[n-D]
// times x times its integral anchored at 96 kHz (sdt.delrmrm), times 10,
// into the 11:1 compressor (sdt.delrmdyn). One D for the four channels:
// DDELAY's distance in metres moved to the next prime at the session's rate
// (sma.imt2npsamp). This file only wires the _common blocks.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_compressors.h"
#include "seam_delays.h"
#include "seam_denormals.h"
#include "seam_filters.h"
#include "seam_primes.h"
#include "seam_ramp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace delrm {

constexpr int    kChannels      = 4;
constexpr double kMaxMetres     = 30.0;     // DDELAY's range
constexpr double kDefaultMetres = 7.291;    // Davide's 22 ms
constexpr double kShortRamp     = 0.025;    // s
// sdt.delrmint: sfi.leakyint(1) scaled by the rate SSCDO#2 is played at.
constexpr double kIntegratorFc    = 1.0;
constexpr double kIntegratorScale = 96000.0;
// sdt.delrmdyn: *(10) : co.compressor_mono(11, -24, 0.03, 0.04).
constexpr double kRmGain = 10.0, kRatio = 11.0, kThreshDb = -24.0, kAttack = 0.03, kRelease = 0.04;
// Meters: instant attack per block, one-pole release across blocks.
constexpr double kMeterRelease = 0.3;       // s
constexpr double kInFloorDb = -70.0, kInTopDb = 5.0, kGrRangeDb = 48.0;

// The largest n of 30 m plus room for one prime gap (1024 is above every
// gap below 2^32).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(kMaxMetres * fs / Seam::kSpeedOfSoundInterior + 0.5) + 1024u;
}

inline uint32_t delayFor(double mt, double fs, const Seam::PrimeSieve& s) {
    return Seam::metresToPrimeSamples(std::min(kMaxMetres, std::max(0.0, mt)), fs, s);
}

// Rounding and the prime above are non-decreasing, so 30 m asks the longest
// D; +1 holds x[n-D] with x[n]. The spec's 1 << 15 holds 30 m up to 192 kHz
// (17 383) but not at 384 kHz (34 763): exact sizing is right at any rate.
inline std::size_t lineLength(double fs, const Seam::PrimeSieve& s) {
    return (std::size_t)delayFor(kMaxMetres, fs, s) + 1;
}

class Engine {
public:
    // Outside the audio thread (setActive). false when the memory is not
    // there: the engine stays silent.
    bool prepare(double fs) {
        release();
        fs_ = fs;
        try {
            sieve_.reset(new Seam::PrimeSieve(sieveBound(fs)));
            len_ = delrm::lineLength(fs, *sieve_);
            mem_.reset(new double[(std::size_t)kChannels * len_]());
        } catch (const std::bad_alloc&) {
            release();
            return false;
        }
        for (int c = 0; c < kChannels; ++c) dl_[c].attach(mem_.get() + (std::size_t)c * len_, len_);
        for (int r = 0; r < 2; ++r) {
            li_[r].prepare(fs, kIntegratorFc);
            cm_[r].prepare(fs, kRatio, kThreshDb, kAttack, kRelease);
        }
        applyDistance();
        out_.setTarget(out_.target(), kShortRamp, fs_);
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        for (int c = 0; c < kChannels; ++c) { heldPeak_[c] = blockPeak_[c] = 0.0; }
        for (int r = 0; r < 2; ++r) { heldGr_[r] = blockGr_[r] = 0.0; }
        sampleRate_.store(fs);
        return true;
    }

    void release() {
        mem_.reset();
        sieve_.reset();
        len_ = 0;
        delay_.store(0);
        sampleRate_.store(0.0);
        // the meters fall to the floor with the engine, they do not freeze
        for (int c = 0; c < kChannels; ++c) { heldPeak_[c] = blockPeak_[c] = 0.0; }
        for (int r = 0; r < 2; ++r) { heldGr_[r] = blockGr_[r] = 0.0; }
    }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { out_.snap(); pow_.snap(); }

    // Audio thread, at the start of a block (or before prepare). The delay
    // jumps, as in the spec.
    void setDistance(double mt) {
        mt = std::min(kMaxMetres, std::max(0.0, mt));
        if (mt == metres_ && applied_) return;
        metres_ = mt;
        if (mem_) applyDistance();
    }
    void setOutput(double v) { if (v != out_.target()) out_.setTarget(v, kShortRamp, fs_); }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!mem_) { zero(out, n); return; }
        // The integrators and the compressors decay in silence: no subnormals.
        Seam::ScopedNoDenormals noDenormals;
        double peak[kChannels] = {}, depth[2] = {};
        for (int k = 0; k < n; ++k) {
            const double g = out_.next() * pow_.next();
            for (int c = 0; c < kChannels; ++c) {
                const double x = (double)in[c][k];
                peak[c] = std::max(peak[c], std::fabs(x));
                const double d = dl_[c].tick(x);
                double y;
                if ((c & 1) == 0) {                                  // channels 1, 3
                    y = x + d;
                } else {                                             // channels 2, 4
                    const int r = c >> 1;
                    const double p = d * x * (kIntegratorScale * li_[r].tick(x));
                    y = cm_[r].tick(kRmGain * p);
                    depth[r] = std::max(depth[r], -cm_[r].gainDb());
                }
                out[c][k] = (T)(y * g);
            }
        }
        const double decay = std::exp(-(double)n / (kMeterRelease * fs_));
        for (int c = 0; c < kChannels; ++c) {
            blockPeak_[c] = peak[c];
            heldPeak_[c] = std::max(peak[c], heldPeak_[c] * decay);
        }
        for (int r = 0; r < 2; ++r) {
            blockGr_[r] = depth[r];
            heldGr_[r] = std::max(depth[r], heldGr_[r] * decay);
        }
    }

    // Readouts. Meters: audio thread (the processor reads them after process).
    bool        prepared() const          { return (bool)mem_; }
    std::size_t lineLength() const        { return len_; }
    uint32_t    delaySamples() const      { return delay_.load(); }
    double      sampleRate() const        { return sampleRate_.load(); }
    double      inputPeak(int c) const    { return heldPeak_[c]; }
    double      reductionDb(int r) const  { return heldGr_[r]; }
    double      blockPeak(int c) const    { return blockPeak_[c]; }
    double      blockReductionDb(int r) const { return blockGr_[r]; }

private:
    void applyDistance() {
        const uint32_t d = delayFor(metres_, fs_, *sieve_);
        for (auto& l : dl_) l.setDelay(d);
        delay_.store(d);
        applied_ = true;
    }

    template <class T>
    static void zero(T* const* out, int n) {
        for (int c = 0; c < kChannels; ++c) std::fill(out[c], out[c] + n, (T)0);
    }

    double fs_ = 96000.0;
    double metres_ = kDefaultMetres;
    bool   applied_ = false;
    std::unique_ptr<double[]> mem_;
    std::size_t len_ = 0;
    std::unique_ptr<Seam::PrimeSieve> sieve_;
    Seam::IntegerDelay    dl_[kChannels];
    Seam::LeakyIntegrator li_[2];
    Seam::CompressorMono  cm_[2];
    Seam::LinearRamp out_, pow_;
    double heldPeak_[kChannels] = {}, blockPeak_[kChannels] = {};
    double heldGr_[2] = {}, blockGr_[2] = {};

    std::atomic<uint32_t> delay_{0};
    std::atomic<double>   sampleRate_{0.0};
};

} // namespace delrm
