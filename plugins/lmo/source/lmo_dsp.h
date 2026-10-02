//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · LMO — the engine (SDK-free)
//
// sdt.lmo(4, f, d) of seam.tedesco.lib: per channel i (0..3), two bands of
// noise, HP24 : LP24 at the same centre, one at f - d/2 + i (held >= 1 Hz),
// one at f + d/2 + i; streams 0..3 of ONE multinoise(72) feed the low bands,
// streams 4..7 the high ones (sdt.lmonoise: blocks 1-2; block 3, streams
// 8..71, is the choir's); channel i = (low + high)/sqrt(2), times the
// density anchor sqrt(fs/96000), times volume and POWER.
//
// Everything that is time is seconds: the ramps (glide for f, 25 ms for
// d, volume and POWER, the ramp of the original's interpolator_4ch), and
// the cadence of the filter redesign, 6 kHz (16 samples at 96 kHz).
// Ramps step every sample; the filters follow every update period, and
// only when f or d has actually moved.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_noise.h"
#include "seam_butterworth.h"
#include "seam_ramp.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace lmo {

constexpr int    kChannels   = 4;
constexpr int    kOrder      = 24;        // the original's Butterworth order
constexpr int    kStreams    = kChannels * (2 + 16);  // sdt.sscdo2streams(4): LMO + the choir's 16 bands per voice
constexpr double kRefRate    = 96000.0;   // SSCDO#2 is played at 96 kHz
constexpr double kShortRamp  = 0.025;     // s
constexpr double kUpdateRate = 6000.0;    // Hz, filter redesign cadence
constexpr double kLpOffset   = 0.0001;    // Hz, the original's LP offset
constexpr double kMinBand    = 1.0;       // Hz, lowest band centre

// White noise of constant RMS spreads its power over fs/2: a band of fixed
// width gets 3.01 dB less per doubling of fs. This factor holds it at its
// 96 kHz level (sdt.lmodens).
inline double densityGain(double fs) { return std::sqrt(fs / kRefRate); }

inline int updatePeriod(double fs) {
    return std::max(1, (int)std::lround(fs / kUpdateRate));
}

// One band of sdt.lmoband: HP24 into LP24 at the same centre.
class Band {
public:
    Band() {
        hp_.setType(Seam::ButterworthType::Highpass);
        lp_.setType(Seam::ButterworthType::Lowpass);
    }
    void set(double fb, double fs) {
        hp_.setFrequency(fb, fs);
        lp_.setFrequency(fb - kLpOffset, fs);
    }
    void reset() { hp_.reset(); lp_.reset(); }
    double tick(double x) { return lp_.tick(hp_.tick(x)); }
private:
    Seam::ButterworthSVF<kOrder> hp_, lp_;
};

class Engine {
public:
    Engine() : noise_(kStreams, 0, 2 * kChannels) {}

    void prepare(double fs) {
        fs_ = fs;
        period_ = updatePeriod(fs);
        density_ = densityGain(fs);
        reset();
    }

    // Noise to its seed, filters to zero, every ramp onto its target.
    void reset() {
        noise_.reset();
        for (auto& b : bands_) b.reset();
        f_.snap(); d_.snap(); vol_.snap(); pow_.snap();
        design(f_.value(), d_.value());
        countdown_ = 0;
    }

    // glide applies to the NEXT frequency target, as Pd's line takes its
    // time before its target: a cue sets glide, then f.
    void setGlide(double seconds) { glide_ = std::max(0.0, seconds); }

    void setFrequency(double hz) {
        if (hz == f_.target()) return;   // automation re-sends; never restart
        f_.setTarget(hz, glide_ > 0.0 ? glide_ : kShortRamp, fs_);
    }
    void setDelta(double hz) {
        if (hz == d_.target()) return;
        d_.setTarget(hz, kShortRamp, fs_);
    }
    void setVolume(double v) {
        if (v == vol_.target()) return;
        vol_.setTarget(v, kShortRamp, fs_);
    }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t == pow_.target()) return;
        pow_.setTarget(t, kShortRamp, fs_);
    }

    double currentFrequency() const { return f_.value(); }

    template <class T>
    void process(T* const* out, int n) {
        double nz[2 * kChannels];
        for (int i = 0; i < n; ++i) {
            if (countdown_ == 0) {
                if (f_.value() != fDesigned_ || d_.value() != dDesigned_)
                    design(f_.value(), d_.value());
                countdown_ = period_;
            }
            --countdown_;
            f_.next();
            d_.next();

            noise_.tick(nz);
            const double g = density_ * vol_.next() * pow_.next();
            for (int c = 0; c < kChannels; ++c) {
                const double lo = bands_[c].tick(nz[c]);
                const double hi = bands_[kChannels + c].tick(nz[kChannels + c]);
                out[c][i] = (T)((lo + hi) / std::sqrt(2.0) * g);
            }
        }
    }

private:
    void design(double f, double d) {
        for (int c = 0; c < kChannels; ++c) {
            bands_[c].set(std::max(kMinBand, f - d / 2.0 + c), fs_);
            bands_[kChannels + c].set(f + d / 2.0 + c, fs_);
        }
        fDesigned_ = f;
        dDesigned_ = d;
    }

    double fs_ = kRefRate, density_ = 1.0, glide_ = 0.0;
    int period_ = 16, countdown_ = 0;
    double fDesigned_ = -1.0, dDesigned_ = -1.0;

    Seam::MultinoiseBlock noise_;   // sdt.lmonoise(4)
    std::array<Band, 2 * kChannels> bands_;
    Seam::LinearRamp f_, d_, vol_, pow_;
};

} // namespace lmo
