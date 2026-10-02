//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · choir — the engine (SDK-free)
//
// sdt.choir(350, 1.5) of seam.tedesco.lib on four channels. Channel c, band
// k = 0..15: the input through a band at f_c*(k+1) and a follower (what the
// choir hears), times noise stream 16c+k of the SSCDO#2 noise's block 3
// through a band at f_c*(k+1)^a_c, held at its 96 kHz level (the voice);
// the 16 products summed and divided by Q*2*pi. A band centred at 20 kHz or
// above is inactive, every band is designed at min(fc, 19999) Hz. No DC
// blocker. This file only wires the _common blocks.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "choir_display.h"
#include "seam_analyzers.h"
#include "seam_denormals.h"
#include "seam_noise.h"
#include "seam_ramp.h"
#include "seam_svf.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace choir {

constexpr int    kChannels     = 4;
constexpr int    kBands        = 16;
constexpr int    kNoiseStreams = 72;          // sdt.sscdo2streams(4): LMO's 8 + the choir's 64
constexpr int    kNoiseOffset  = 8;           // block 3 starts after LMO's two blocks
constexpr double kRefRate      = 96000.0;     // SSCDO#2 is played at 96 kHz
constexpr double kSilentFrom   = 20000.0;     // Hz, sdt.choirband
constexpr double kDesignCeil   = 19999.0;     // Hz
constexpr double kShortRamp    = 0.025;       // s

struct Config {
    std::array<double, kChannels> f{{48.0, 48.0, 96.0, 96.0}};      // sdt.choirf
    std::array<double, kChannels> a{{1.0, 1.01, 1.1, 0.9}};         // sdt.choira
    double q = 350.0;
    double release = 1.5;                                           // s
};

class Engine {
public:
    explicit Engine(Config cfg = Config())
        : cfg_(cfg), noise_(kNoiseStreams, kNoiseOffset, kChannels * kBands) {}

    // Outside the audio thread (setActive): designs every band, zeroes every
    // state, rewinds the noise to the generator's start.
    void prepare(double fs) {
        fs_ = fs;
        dens_ = std::sqrt(fs / kRefRate);
        qnorm_ = cfg_.q * 2.0 * M_PI;
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k) {
                const double fl = cfg_.f[(size_t)c] * (k + 1);
                const double fv = cfg_.f[(size_t)c] * std::pow(k + 1.0, cfg_.a[(size_t)c]);
                listen_[c][k].design(fs, std::min(fl, kDesignCeil), cfg_.q);
                sing_[c][k].design(fs, std::min(fv, kDesignCeil), cfg_.q);
                follow_[c][k].prepare(fs, cfg_.release);
                active_[c][k] = fl < kSilentFrom && fv < kSilentFrom;
                display_.store(c, k, active_[c][k] ? 0.0 : -1.0);
            }
        clearStates();
        noise_.reset();
        served_ = resetGen_.load();
        out_.setTarget(out_.target(), kShortRamp, fs_);
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        sampleRate_.store(fs);
        prepared_ = true;
    }

    void release() { prepared_ = false; sampleRate_.store(0.0); }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { out_.snap(); pow_.snap(); }

    void setOutput(double v) { if (v != out_.target()) out_.setTarget(v, kShortRamp, fs_); }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    // GUI thread: served at the start of the next block.
    void requestReset() { resetGen_.fetch_add(1); }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!prepared_) {
            for (int c = 0; c < kChannels; ++c) std::fill(out[c], out[c] + n, (T)0);
            return;
        }
        // 128 bands and 64 followers decay in silence: no subnormals.
        Seam::ScopedNoDenormals noDenormals;
        const uint32_t g = resetGen_.load();
        if (g != served_) { clearStates(); served_ = g; }

        double peak[kChannels][kBands] = {};
        double nz[kChannels * kBands];
        for (int i = 0; i < n; ++i) {
            const double gain = out_.next() * pow_.next();
            noise_.tick(nz);
            for (int c = 0; c < kChannels; ++c) {
                const double x = (double)in[c][i];
                double acc = 0.0;
                for (int k = 0; k < kBands; ++k) {
                    if (!active_[c][k]) continue;
                    const double e = follow_[c][k].tick(listen_[c][k].tick(x));
                    const double v = sing_[c][k].tick(nz[c * kBands + k]) * dens_;
                    acc += e * v;
                    peak[c][k] = std::max(peak[c][k], e);
                }
                out[c][i] = (T)(acc / qnorm_ * gain);
            }
        }
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k)
                if (active_[c][k]) display_.store(c, k, peak[c][k] / cfg_.q);
    }

    bool    prepared() const           { return prepared_; }
    double  sampleRate() const         { return sampleRate_.load(); }
    double  gain() const               { return out_.value() * pow_.value(); }
    bool    active(int c, int k) const { return active_[c][k]; }
    const Display& display() const     { return display_; }
    const Config&  config() const      { return cfg_; }

    bool hasSubnormalState() const {
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k)
                if (listen_[c][k].hasSubnormalState() || sing_[c][k].hasSubnormalState() ||
                    follow_[c][k].hasSubnormalState()) return true;
        return false;
    }

private:
    void clearStates() {
        for (int c = 0; c < kChannels; ++c)
            for (int k = 0; k < kBands; ++k) {
                listen_[c][k].reset(); sing_[c][k].reset(); follow_[c][k].reset();
            }
    }

    Config cfg_;
    double fs_ = kRefRate, dens_ = 1.0, qnorm_ = 350.0 * 2.0 * M_PI;
    bool   prepared_ = false;
    Seam::SvfBandpass listen_[kChannels][kBands], sing_[kChannels][kBands];
    Seam::AmpFollower follow_[kChannels][kBands];
    bool   active_[kChannels][kBands] = {};
    Seam::MultinoiseBlock noise_;
    Seam::LinearRamp out_, pow_;
    Display display_;
    std::atomic<uint32_t> resetGen_{0};
    uint32_t served_ = 0;
    std::atomic<double> sampleRate_{0.0};
};

} // namespace choir
