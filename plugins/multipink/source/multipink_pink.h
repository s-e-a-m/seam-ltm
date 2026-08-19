// SEAM-LTM · multipink_pink — the pinking filter, SDK-free so it can be judged.
//
// FAUST REFERENCE (seam.filters.lib): sfi.spectral_tilt_mz, sfi.pink_filter_mz
//
// A cascade of first-order real pole-zero pairs spaced geometrically in Hz
// from 2 Hz to fs/2, alpha = -1/2, mapped with the MATCHED-Z transform, plus
// one correction section whose coefficients never change with the sample rate.
//
// Matched-Z rather than bilinear, and the reason is measurable: the bilinear
// transform warps the frequency axis, and at 16 kHz with fs = 48 kHz the warp
// is tan(60 deg)/(pi/3) = 1.654, which is 0.73 octaves, which at -3.01 dB per
// octave is -2.19 dB. The poles land where they are asked to; the curve
// between them does not. Matched-Z leaves the axis alone and errs the other
// way, by the aliased tail of a response that decays only 3 dB per octave --
// an error which is a function of f/fs ALONE, verified identical at 48 kHz and
// 192 kHz to three decimal places, and therefore cancelled once and for all by
// a section with constant coefficients.
//
// Full derivation and the measurements behind every number:
//   docs/superpowers/specs/2026-08-19-pink-filter-mz-design.md
//   doc/study/sessions/2026-08-19-pink-filter-design.md
#pragma once

#include <cmath>
#include <complex>
#include <cstddef>

namespace Seam { namespace multipink {

class PinkDesign {
public:
    static constexpr int    kMaxSections    = 32;
    static constexpr double kLadderF0Hz     = 2.0;
    // Changing this desynchronises sfi.pink_filter_mz in seam.filters.lib,
    // which hardcodes one pole per octave; tools/faust-pink-ab.sh is what
    // will tell you.
    static constexpr double kPolesPerOctave = 1.0;
    static constexpr double kAnchorHz       = 1000.0;

    // The universal correction. Fitted once against the ladder's residual in
    // normalised frequency; it is not a function of fs and must not be redesigned.
    static constexpr double kCorrZero = -0.250775213;
    static constexpr double kCorrPole = -0.160124183;

    // y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1], one section per entry.
    double b0[kMaxSections] = {};
    double b1[kMaxSections] = {};
    double a1[kMaxSections] = {};
    int    numSections = 0;

    void design(double fs) {
        sampleRate_ = fs;
        const double T  = 1.0 / fs;
        const double f1 = 0.5 * fs;
        const double w0 = 2.0 * M_PI * kLadderF0Hz;

        int n = (int)std::ceil(kPolesPerOctave * std::log2(f1 / kLadderF0Hz)) + 1;
        if (n > kMaxSections - 1) n = kMaxSections - 1;   // one slot for the correction
        if (n < 2) n = 2;
        const double r = std::pow(f1 / kLadderF0Hz, 1.0 / (double)(n - 1));

        int k = 0;
        for (int i = 0; i < n; ++i, ++k) {
            const double mz = w0 * std::pow(r, 0.5 + (double)i);   // alpha = -1/2
            const double mp = w0 * std::pow(r, (double)i);
            const double zz = std::exp(-mz * T);
            const double zp = std::exp(-mp * T);
            const double g  = (1.0 - zp) / (1.0 - zz);             // unity gain at DC
            b0[k] =  g;
            b1[k] = -g * zz;
            a1[k] = -zp;
        }
        {
            const double g = (1.0 - kCorrPole) / (1.0 - kCorrZero);
            b0[k] =  g;
            b1[k] = -g * kCorrZero;
            a1[k] = -kCorrPole;
            ++k;
        }
        numSections = k;
    }

    double magnitudeDb(double f) const {
        const std::complex<double> z =
            std::exp(std::complex<double>(0.0, -2.0 * M_PI * f / sampleRate_));
        std::complex<double> H(1.0, 0.0);
        for (int i = 0; i < numSections; ++i)
            H *= (b0[i] + b1[i] * z) / (1.0 + a1[i] * z);
        return 20.0 * std::log10(std::abs(H));
    }

    // Gain at the calibration anchor. Invariant across sample rates by design,
    // which is what lets the calibration offset stay a single constant.
    double anchorGainDb() const { return magnitudeDb(kAnchorHz); }

    // 10*log10 of the mean of |H|^2 over 0..fs/2 — the filter's RMS gain on
    // white input. Falls with fs, which is the fact the calibration must not
    // follow. Midpoint rule over a fixed grid: smooth integrand, and the
    // value is reported rather than used in the audio path.
    double rmsGainDb() const {
        const int kSteps = 200000;
        double sum = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            const double f = (i + 0.5) * (sampleRate_ * 0.5) / kSteps;
            const double m = std::pow(10.0, magnitudeDb(f) / 20.0);
            sum += m * m;
        }
        return 10.0 * std::log10(sum / kSteps);
    }

    double sampleRate() const { return sampleRate_; }

private:
    double sampleRate_ = 48000.0;
};

// The block operation the audio path performs, in one place so that what
// ships and what is tested are the same code. Before this existed the
// recurrence lived four times over -- processor, test, IR dump, benchmark --
// and a whole-branch review showed that swapping the state indices, flipping
// the sign of a1 or dropping the state write-back in the shipped copy left
// the suite entirely green.
//
// scratch holds numStreams rows of numSamples samples, row r at
// scratch[r*numSamples]; every row is filtered IN PLACE.
//
// state is SECTION-MAJOR -- state[sec*stateStride + stream] -- because the
// processor's own state array is [kMaxSections][kPoolSize] and the layout is
// a precondition for a later SIMD pass over the 64 streams of one section.
// It must hold at least d.numSections*stateStride entries, and stateStride
// must be >= numStreams. Zero it to start from silence.
//
// The loop order is section-outer, channel-middle, sample-inner: the
// production order, deliberately kept. tools/bench-pink.cpp measures an
// interchanged order that is faster; adopting it is separate work.
//
// State defaults to float, which is what the plug-in uses at BOTH sample
// sizes; the double instantiation exists for the tools and for the test that
// measures what single precision costs.
//
// No allocation, no locking, no design() call: safe on the audio thread.
template <typename Sample, typename State = float>
inline void pinkFilterBlock(const PinkDesign& d,
                            Sample* scratch, int numStreams, int numSamples,
                            State* state, int stateStride) {
    for (int sec = 0; sec < d.numSections; ++sec) {
        const State b0 = (State)d.b0[sec];
        const State b1 = (State)d.b1[sec];
        const State a1 = (State)d.a1[sec];
        State* st = state + (size_t)sec * (size_t)stateStride;
        for (int ch = 0; ch < numStreams; ++ch) {
            Sample* row = scratch + (size_t)ch * (size_t)numSamples;
            State s = st[ch];
            for (int n = 0; n < numSamples; ++n) {
                // Single precision even under kSample64: State is float on the
                // audio path at both sample sizes, and the cost measured
                // against a double-state run is 0.0008 dB
                // (multipink_pink_engine_test).
                const State x = (State)row[n];
                const State y = b0 * x + s;
                s = b1 * x - a1 * y;
                row[n] = (Sample)y;
            }
            st[ch] = s;
        }
    }
}

} } // namespace Seam::multipink
