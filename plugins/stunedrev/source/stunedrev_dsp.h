//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the engine (SDK-free)
//
// Four independent lines of 42 Moorer all-pass sections in series, each
// line tuned by an irrational ratio k (sdt.stunedrev, seam.tedesco.lib).
// The delay of section i is ms·(i+1)·k, rounded to the sample and moved to
// the next prime at the session's rate (sdt.stdel); each section's buffer is
// sized exactly for the longest delay the slider can ask, 100 ms, in one
// arena allocated outside the audio thread.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_denormals.h"
#include "seam_moorer.h"
#include "seam_primes.h"
#include "seam_ramp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace stunedrev {

constexpr int    kLines    = 4;
constexpr int    kSections = 42;
constexpr int    kTMin = 1, kTMax = 100;                     // ms, the score's slider
constexpr int    kDefaultTimes[kLines] = { 83, 47, 7, 71 };  // the Pd patch's start
constexpr double kShortRamp = 0.025;                          // s
// RESET zeroes the arena at this many bytes per sample of the block: the
// clearing lasts the same time at any block size and rate (588 MiB at
// 96 kHz in 0.39 s), and asks memset for 1.5 GB/s, a fraction of its speed.
constexpr std::size_t kClearBytesPerSample = 16384;

// g = 1/sqrt(2) as in the original; the ratios in the original's line order.
// ma.E and ma.PI are these doubles.
inline const double kG = 1.0 / std::sqrt(2.0);
inline const double kRatio[kLines] = {
    std::sqrt(2.0), (1.0 + std::sqrt(5.0)) / 2.0, 2.718281828459045, 3.141592653589793 };

// sdt.stdel(k, i, ms) = sma.ms2npsamp(ms*(i+1)*k): the product before the prime.
inline uint32_t sectionDelay(int line, int i, double ms, double fs, const Seam::PrimeSieve& s) {
    return Seam::msToPrimeSamples(ms * (i + 1) * kRatio[line], fs, s);
}

// Rounding and the prime above are both non-decreasing, so the longest
// delay of a section is the one at 100 ms; +1 holds w[n-(t-1)] with w[n].
// sdt.stmd sizes with a fixed +150 instead, because Faust sizes buffers at
// compile time and cannot evaluate sff.np there; exact sizing is right at
// any rate (at 384 kHz a prime gap of 154 would exceed the +150).
inline std::size_t sectionLength(int line, int i, double fs, const Seam::PrimeSieve& s) {
    return (std::size_t)sectionDelay(line, i, kTMax, fs, s) + 1;
}

// The largest n of the plugin (100 ms, section 42, pi) plus room for one
// prime gap: 1024 is above every gap below 2^32 (the largest is 336).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(kTMax * kSections * kRatio[3] * fs / 1000.0 + 0.5) + 1024u;
}

// Each section is a Seam::MoorerAllpass (seam_moorer.h), g = kG, attached
// to its slice of the arena.
using Section = Seam::MoorerAllpass;

enum class Status : int { Unprepared = 0, Ready, Clearing, AllocFailed };

class Engine {
public:
    Engine() { for (int j = 0; j < kLines; ++j) time_[j] = kDefaultTimes[j]; }

    // Outside the audio thread (setActive). Sieve, arena (zeroed now, so
    // every page is resident before process() runs), delays. false when the
    // memory is not there: the engine stays silent.
    bool prepare(double fs) {
        release();
        fs_ = fs;
        sampleRate_.store(fs);
        try {
            sieve_.reset(new Seam::PrimeSieve(sieveBound(fs)));
            std::size_t total = 0;
            for (int j = 0; j < kLines; ++j)
                for (int i = 0; i < kSections; ++i) total += sectionLength(j, i, fs, *sieve_);
            arena_.reset(new double[total]());
            arenaSize_ = total;
        } catch (const std::bad_alloc&) {
            release();
            status_.store(Status::AllocFailed);
            return false;
        }
        std::size_t off = 0;
        for (int j = 0; j < kLines; ++j)
            for (int i = 0; i < kSections; ++i) {
                const std::size_t len = sectionLength(j, i, fs, *sieve_);
                sec_[j][i].attach(arena_.get() + off, len);
                sec_[j][i].setGain(kG);
                off += len;
            }
        arenaBytes_.store(arenaSize_ * sizeof(double));
        for (int j = 0; j < kLines; ++j) applyTime(j);
        in_.setTarget(in_.target(), kShortRamp, fs_);
        out_.setTarget(out_.target(), kShortRamp, fs_);
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        fade_.setTarget(1.0, kShortRamp, fs_);
        phase_ = Phase::Run;
        clearPos_ = 0;
        servedGen_ = resetGen_.load();       // a click before prepare is not replayed
        status_.store(Status::Ready);
        return true;
    }

    void release() {
        arena_.reset();
        sieve_.reset();
        arenaSize_ = 0;
        arenaBytes_.store(0);
        status_.store(Status::Unprepared);
    }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { in_.snap(); out_.snap(); pow_.snap(); fade_.snap(); }

    // Audio thread, at the start of a block (or before prepare). The delays
    // jump, as in the spec: the buffers hold the whole history.
    void setTime(int line, int ms) {
        ms = std::min(kTMax, std::max(kTMin, ms));
        if (ms == time_[line] && applied_[line]) return;
        time_[line] = ms;
        if (arena_) applyTime(line);
    }
    void setInput(double v)  { if (v != in_.target())  in_.setTarget(v, kShortRamp, fs_); }
    void setOutput(double v) { if (v != out_.target()) out_.setTarget(v, kShortRamp, fs_); }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    // Any thread (the RESET view): served at the start of the next block.
    void requestReset() { resetGen_.fetch_add(1); }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!arena_) { zero(out, n); return; }
        // The lines lose no energy: without this their silent tails would
        // fill the arena with subnormals, and the cost would grow for hours.
        Seam::ScopedNoDenormals noDenormals;
        const uint32_t gen = resetGen_.load();
        if (gen != servedGen_) {                  // a click: fade, then clear
            servedGen_ = gen;
            phase_ = Phase::FadeOut;
            fade_.setTarget(0.0, kShortRamp, fs_);
            status_.store(Status::Clearing);
        }
        if (phase_ == Phase::Clear) {
            clearChunk(n);
            for (int k = 0; k < n; ++k) { in_.next(); out_.next(); pow_.next(); }
            zero(out, n);
            return;
        }
        const bool feeding = phase_ == Phase::Run;
        for (int k = 0; k < n; ++k) {
            const double gin  = in_.next();
            const double gout = out_.next() * pow_.next() * fade_.next();
            double x[kLines];
            for (int j = 0; j < kLines; ++j) x[j] = feeding ? (double)in[j][k] * gin : 0.0;
            for (int j = 0; j < kLines; ++j) {
                double y = x[j];
                for (Section& s : sec_[j]) y = s.tick(y);
                out[j][k] = (T)(y * gout);
            }
        }
        if (phase_ == Phase::FadeOut && !fade_.active()) {
            phase_ = Phase::Clear;                // the lines freeze from the next block
            clearPos_ = 0;
        }
    }

    // Readouts.
    int         time(int line) const           { return time_[line]; }
    uint32_t    delay(int line, int i) const   { return sec_[line][i].delay(); }
    double      centroidSeconds(int line) const { return centroid_[line].load(); }
    std::size_t arenaBytes() const             { return arenaBytes_.load(); }
    double      sampleRate() const             { return sampleRate_.load(); }
    Status      status() const                 { return status_.load(); }

private:
    enum class Phase { Run, FadeOut, Clear };

    void applyTime(int line) {
        double sum = 0.0;
        for (int i = 0; i < kSections; ++i) {
            const uint32_t t = sectionDelay(line, i, time_[line], fs_, *sieve_);
            sec_[line][i].setDelay(t);
            sum += t;
        }
        centroid_[line].store(sum / fs_);   // each section delays the energy by its t on average
        applied_[line] = true;
    }

    // A slice of the arena per block, proportional to the block: the lines
    // are frozen, so no uncleared history can flow into a cleared buffer.
    void clearChunk(int n) {
        const std::size_t want = (std::size_t)n * kClearBytesPerSample / sizeof(double);
        const std::size_t m = std::min(want, arenaSize_ - clearPos_);
        std::memset(arena_.get() + clearPos_, 0, m * sizeof(double));
        clearPos_ += m;
        if (clearPos_ < arenaSize_) return;
        for (auto& line : sec_) for (Section& s : line) s.clear();
        phase_ = Phase::Run;
        fade_.setTarget(1.0, kShortRamp, fs_);
        status_.store(Status::Ready);
    }

    template <class T>
    static void zero(T* const* out, int n) {
        for (int j = 0; j < kLines; ++j) std::fill(out[j], out[j] + n, (T)0);
    }

    double fs_ = 96000.0;
    int    time_[kLines] = {};
    bool   applied_[kLines] = {};
    Section sec_[kLines][kSections];
    std::unique_ptr<double[]> arena_;
    std::size_t arenaSize_ = 0, clearPos_ = 0;
    std::unique_ptr<Seam::PrimeSieve> sieve_;
    Seam::LinearRamp in_, out_, pow_, fade_;
    Phase phase_ = Phase::Run;
    uint32_t servedGen_ = 0;

    std::atomic<uint32_t>    resetGen_{0};
    std::atomic<Status>      status_{Status::Unprepared};
    std::atomic<double>      centroid_[kLines] = {};
    std::atomic<std::size_t> arenaBytes_{0};
    std::atomic<double>      sampleRate_{0.0};
};

} // namespace stunedrev
