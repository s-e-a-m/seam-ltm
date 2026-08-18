// SEAM-LTM · strx_bands — SDK-free ISO 266 third-octave band analysis.
//
// FAUST REFERENCE:
//   seam.analyzers.lib : san.tob_fc, san.tob_tau            — the ISO 266 grid
//                                                             and its per-band
//                                                             averaging time
//   seam.analyzers.lib : san.thirdoctave_levels_ab          — the filterbank
//   seam.analyzers.lib : san.pinkdev, san.pinkcomp          — the error curve
//   filters.lib        : fi.bandpass6e, fi.tf2sb, fi.tf1sb  — the band filter
//
// Reads a pink-noise measurement made through a spaced microphone pair and
// reports, per ISO band, the dB to dial into the power amplifier. Pink noise
// carries equal energy in every constant-relative-bandwidth band, so a correct
// loudspeaker reads flat and the mean of the bands is the only reference the
// measurement needs — absolute level, microphone sensitivity and distance all
// fall out with it, and so does any smooth error of the microphone itself.
//
// The numbers are BAND-INTEGRATED deviations, so they are gains for a band
// filter and not the depth of a narrow notch: a 6 dB dip one third of an
// octave wide reads about 3.5 dB. The table under-reports rather than over-
// reports, which is the safe direction to converge from when the correction is
// applied and the measurement repeated.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Seam { namespace strx {

// ---------------------------------------------------------------- the grid --
// 31 ISO 266 bands, index i = 0 at 20 Hz (band n = -17) up to 20 kHz (n = 13).
inline constexpr int kNumBands = 31;
inline constexpr int kBandN0   = -17;

// The bands that set the zero: 31.5 Hz (i=2) .. 16 kHz (i=29). The three
// extreme bands are drawn but kept out of the mean, where their noise would
// drag the whole table.
inline constexpr int kNormI0 = 2;
inline constexpr int kNormNi = 28;

// Exact ISO 266 midband frequency. The base-ten grid (10^(n/10)) is the one
// ISO 266 prefers; a base-two grid (2^(n/3)) drifts from it by 0.08% a band.
inline double bandFc(int i) { return 1000.0 * std::pow(10.0, (kBandN0 + i) / 10.0); }
inline double bandFl(int i) { return bandFc(i) * std::pow(10.0, -0.05); }
inline double bandFuNominal(int i) { return bandFc(i) * std::pow(10.0, 0.05); }
// Clamped just under Nyquist so the band count stays fixed across sample
// rates: at 44.1 kHz the 20 kHz band would otherwise ask for a 22.4 kHz corner
// that cannot be designed. The top band narrows instead of the bank breaking.
inline double bandFu(int i, double fs) {
    return std::min(bandFuNominal(i), 0.49 * fs);
}

// A band is only measurable when its WHOLE passband sits comfortably below
// Nyquist. Measured on the running analyser (2026-08-18), reading pink through
// the bank at three rates, the top band's error against the ratio fu/Nyquist:
//
//     fu/Nyquist   0.47   0.74   0.81   0.93   1.02
//     error (dB)  +0.15  +0.20  -0.12  -0.65  -3.01
//
// Past ~0.85 the bilinear transform warps the design enough that the reading
// describes the sample rate instead of the loudspeaker, so those bands are not
// reported at all. Dropping them beats clamping them: a clamped band is a
// narrower band, and a narrower band is a number that looks usable and is not.
// At 44.1 and 48 kHz this costs the 20 kHz band; at 96 kHz it costs nothing.
inline constexpr double kNyquistMargin = 0.85;
inline bool bandMeasurable(int i, double fs) {
    return bandFuNominal(i) <= kNyquistMargin * 0.5 * fs;
}
// Bands are ordered, so measurability is a prefix: [0, measurableBands) is the
// whole usable table.
inline int measurableBands(double fs) {
    int n = 0;
    while (n < kNumBands && bandMeasurable(n, fs)) ++n;
    return n;
}

// Per-band averaging time. Noise-level confidence follows the bandwidth-time
// product, so one global tau leaves the low bands rattling while the high ones
// lag; tau = BT/B equalises it across the bank (~11 s at 20 Hz, ~0.22 s at
// 1 kHz). BT = 50 is about +/-0.6 dB.
inline constexpr double kBandBT = 50.0;
inline double bandTau(int i, double fs) {
    const double bw = bandFu(i, fs) - bandFl(i);
    return std::clamp(kBandBT / bw, 0.1, 20.0);
}

// ------------------------------------------------------------ band filter --
// Hand port of fi.bandpass6e: a third-order elliptic lowpass prototype mapped
// to a bandpass by fi.tf2sb (2nd order -> 4th) and fi.tf1sb (1st -> 2nd).
// Kept in double throughout, and not by preference: compiled in single
// precision the same filterbank puts every band at or below 160 Hz on the
// epsilon floor, because a 4.6 Hz wide 6th-order section at 20 Hz is hopelessly
// ill conditioned in direct form at 32 bits. Measured, not assumed — see the
// numerical note in seam.analyzers.lib.
class Bandpass6e {
public:
    void design(double fl, double fu, double fs) {
        // Elliptic prototype (filters.lib, bandpass6e).
        constexpr double a11 = 0.802636764161030;
        constexpr double a01 = 1.412270893774204;
        constexpr double a02 = 0.822445908998816;
        constexpr double b21 = 0.019809144837789;
        constexpr double b11 = 0.0;
        constexpr double b01 = 1.161516418982696;

        const double c   = 2.0 * fs;
        const double wla = c * std::tan(2.0 * M_PI * fl / c);   // s-plane edges
        const double wua = c * std::tan(2.0 * M_PI * fu / c);
        const double wc  = std::sqrt(wla * wua);                // s-plane centre
        const double w1  = wua - wc * wc / wua;                 // s-plane LP cutoff
        const double T   = 1.0 / fs;

        // fi.tf2sb(b21, b11, b01, a11, a01, w1, wc) -> 4th-order section.
        {
            const double b2 = b21, b1 = b11, b0 = b01, a1 = a11, a0 = a01;
            const double T2 = T*T, T3 = T2*T, T4 = T3*T;
            const double w12 = w1*w1, wc2 = wc*wc, wc4 = wc2*wc2;
            const double b0d = (4*b0*w12 + 8*b2*wc2)*T2 + 8*b1*w1*T + 16*b2;
            const double b1d = 4*b2*wc4*T4 + 4*b1*wc2*w1*T3 - 16*b1*w1*T - 64*b2;
            const double b2d = 6*b2*wc4*T4 + (-8*b0*w12 - 16*b2*wc2)*T2 + 96*b2;
            const double b3d = 4*b2*wc4*T4 - 4*b1*wc2*w1*T3 + 16*b1*w1*T - 64*b2;
            const double b4d = (b2*wc4*T4 - 2*b1*wc2*w1*T3
                                + (4*b0*w12 + 8*b2*wc2)*T2 - 8*b1*w1*T + 16*b2)
                               + b2*wc4*T4 + 2*b1*wc2*w1*T3;
            const double a0d = wc4*T4 + 2*a1*wc2*w1*T3 + (4*a0*w12 + 8*wc2)*T2 + 8*a1*w1*T + 16;
            const double a1d = 4*wc4*T4 + 4*a1*wc2*w1*T3 - 16*a1*w1*T - 64;
            const double a2d = 6*wc4*T4 + (-8*a0*w12 - 16*wc2)*T2 + 96;
            const double a3d = 4*wc4*T4 - 4*a1*wc2*w1*T3 + 16*a1*w1*T - 64;
            const double a4d = wc4*T4 - 2*a1*wc2*w1*T3 + (4*a0*w12 + 8*wc2)*T2 - 8*a1*w1*T + 16;
            B_[0] = b0d/a0d; B_[1] = b1d/a0d; B_[2] = b2d/a0d;
            B_[3] = b3d/a0d; B_[4] = b4d/a0d;
            A_[0] = a1d/a0d; A_[1] = a2d/a0d; A_[2] = a3d/a0d; A_[3] = a4d/a0d;
        }
        // fi.tf1sb(0, 1, a02, w1, wc) -> biquad.
        {
            const double b1 = 0.0, b0 = 1.0, a0 = a02;
            const double T2 = T*T, wc2 = wc*wc;
            const double a0d = wc2*T2 + 2*a0*w1*T + 4;
            const double c0d = b1*wc2*T2 + 2*b0*w1*T + 4*b1;
            const double c1d = 2*b1*wc2*T2 - 8*b1;
            const double c2d = b1*wc2*T2 - 2*b0*w1*T + 4*b1;
            const double d1d = 2*wc2*T2 - 8;
            const double d2d = wc2*T2 - 2*a0*w1*T + 4;
            C_[0] = c0d/a0d; C_[1] = c1d/a0d; C_[2] = c2d/a0d;
            D_[0] = d1d/a0d; D_[1] = d2d/a0d;
        }
        reset();
    }

    void reset() {
        for (int k = 0; k < 4; ++k) { x_[k] = 0.0; y_[k] = 0.0; }
        for (int k = 0; k < 2; ++k) { u_[k] = 0.0; v_[k] = 0.0; }
    }

    double process(double x) {
        const double y = B_[0]*x + B_[1]*x_[0] + B_[2]*x_[1] + B_[3]*x_[2] + B_[4]*x_[3]
                       - A_[0]*y_[0] - A_[1]*y_[1] - A_[2]*y_[2] - A_[3]*y_[3];
        x_[3] = x_[2]; x_[2] = x_[1]; x_[1] = x_[0]; x_[0] = x;
        y_[3] = y_[2]; y_[2] = y_[1]; y_[1] = y_[0]; y_[0] = y;
        const double v = C_[0]*y + C_[1]*u_[0] + C_[2]*u_[1] - D_[0]*v_[0] - D_[1]*v_[1];
        u_[1] = u_[0]; u_[0] = y;
        v_[1] = v_[0]; v_[0] = v;
        return v;
    }

private:
    double B_[5] = {}, A_[4] = {};      // 4th-order section
    double C_[3] = {}, D_[2] = {};      // biquad
    double x_[4] = {}, y_[4] = {};
    double u_[2] = {}, v_[2] = {};
};


// ------------------------------------------------------- the band analyser --
// Hand port of san.thirdoctave_levels_ab : san.pinkcomp. The two capsules are
// averaged in POWER, band by band, and never summed to Mid: two spaced omnis
// decorrelate above c/2d (430 Hz at 40 cm), so a Mid sum combs by geometry
// right where the measurement is being read.
class BandLevels {
public:
    void prepare(double fs) {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        count_ = measurableBands(fs_);
        for (int i = 0; i < kNumBands; ++i) {
            const double fl = bandFl(i), fu = bandFu(i, fs_);
            bpL_[i].design(fl, fu, fs_);
            bpR_[i].design(fl, fu, fs_);
            tau_[i]  = bandTau(i, fs_);
            pole_[i] = std::exp(-1.0 / (tau_[i] * fs_));   // ba.tau2pole
        }
        reset();
    }

    void reset() {
        for (int i = 0; i < kNumBands; ++i) {
            bpL_[i].reset(); bpR_[i].reset();
            msL_[i] = 0.0; msR_[i] = 0.0;
        }
        elapsed_ = 0;
    }

    // Band-outer, sample-inner: each band keeps its six state variables and
    // two coefficient sets hot for the whole block instead of thrashing 31
    // filters per sample.
    void process(const float* L, const float* R, int n) {
        if (n <= 0) return;
        for (int i = 0; i < kNumBands; ++i) {
            const double p = pole_[i], q = 1.0 - p;
            double ml = msL_[i], mr = msR_[i];
            Bandpass6e& bl = bpL_[i];
            Bandpass6e& br = bpR_[i];
            for (int s = 0; s < n; ++s) {
                const double a = bl.process((double)L[s]);
                const double b = br.process((double)R[s]);
                ml = q * (a * a) + p * ml;      // si.smooth on the squared band
                mr = q * (b * b) + p * mr;
            }
            msL_[i] = ml; msR_[i] = mr;
        }
        elapsed_ += n;
    }

    // The dB to dial into the amplifier, per band, plus how far each band has
    // come towards a trustworthy reading (0..1, three time constants).
    // Deliberately NOT the deviation: the sign is the one thing a comment
    // cannot enforce, so it is applied once, here.
    void compensation(float* comp, float* settle, float* levelDb = nullptr) const {
        double lvl[kNumBands];
        for (int i = 0; i < kNumBands; ++i) {
            const double ms = 0.5 * (msL_[i] + msR_[i]);
            lvl[i] = 10.0 * std::log10(std::max(ms, 1e-30));
        }
        double mean = 0.0;
        for (int i = kNormI0; i < kNormI0 + kNormNi; ++i) mean += lvl[i];
        mean /= (double)kNormNi;
        const double sec = (double)elapsed_ / fs_;
        for (int i = 0; i < kNumBands; ++i) {
            comp[i]   = (float)(mean - lvl[i]);
            settle[i] = (float)std::clamp(sec / (3.0 * tau_[i]), 0.0, 1.0);
            // The raw band level goes out too. It is meaningless in absolute
            // terms -- microphone sensitivity and distance are unknown -- but
            // it is what tells a reader whether a band had any signal at all,
            // and a beautifully flat table measured on noise is still noise.
            if (levelDb) levelDb[i] = (float)lvl[i];
        }
    }

    double secondsObserved() const { return (double)elapsed_ / fs_; }
    // How many leading bands this sample rate can actually measure.
    int count() const { return count_; }

private:
    Bandpass6e bpL_[kNumBands], bpR_[kNumBands];
    double msL_[kNumBands] = {}, msR_[kNumBands] = {};
    double pole_[kNumBands] = {}, tau_[kNumBands] = {};
    double fs_ = 48000.0;
    int count_ = kNumBands;
    int64_t elapsed_ = 0;
};

} } // namespace Seam::strx
