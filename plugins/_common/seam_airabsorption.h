//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · seam_airabsorption — atmospheric absorption (ISO 9613-1)
//
// Atmospheric absorption coefficient alpha(f, T, RH, p) in dB/m, and the
// minimum-phase filters that render alpha*d over distance. The magnitude
// target is physics; the phase is the minimum phase that physics implies.
//
// ISO REFERENCE: ISO 9613-1:1993 "Attenuation of sound during propagation
// outdoors — Part 1: Calculation of the absorption of sound by the
// atmosphere." Constants below are TRANSCRIBED and MUST be verified
// constant-by-constant against the standard (Bass, Sutherland, Zuckerwar
// behind it). See doc/math/ (documentation debt).
//
// FAUST REFERENCE (seam.filters.lib): the air-absorption function (roadmap).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <algorithm>
#include <complex>

namespace Seam { namespace air {

// ISO 9613-1 atmospheric absorption coefficient, dB per metre.
//   fHz       : frequency (Hz)
//   tempC     : temperature (deg C)
//   rhPercent : relative humidity (%)
//   paKPa     : atmospheric pressure (kPa), default 1 atm
inline double alphaISO9613(double fHz, double tempC, double rhPercent,
                           double paKPa = 101.325) {
    const double pr = 101.325;                 // reference pressure, kPa
    const double T  = tempC + 273.15;          // temperature, K
    const double T0 = 293.15;                   // reference temperature (20 C), K
    const double T01 = 273.16;                  // triple-point isotherm, K
    const double f2 = fHz * fHz;
    const double pRatio = paKPa / pr;
    const double tRatio = T / T0;

    // Saturation vapour pressure ratio psat/pr (ISO 9613-1 Annex B).
    const double C = -6.8346 * std::pow(T01 / T, 1.261) + 4.6151;
    const double psatRatio = std::pow(10.0, C);

    // Molar concentration of water vapour, % (h).
    const double h = rhPercent * psatRatio / pRatio;

    // Relaxation frequencies (oxygen, nitrogen).
    const double frO = pRatio * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = pRatio * std::pow(tRatio, -0.5)
                     * (9.0 + 280.0 * h * std::exp(-4.170 * (std::pow(tRatio, -1.0/3.0) - 1.0)));

    // Absorption coefficient, nepers-based term times 8.686 -> dB/m.
    const double classical = 1.84e-11 * (1.0 / pRatio) * std::sqrt(tRatio);
    const double oxygen = 0.01275 * std::exp(-2239.1 / T) / (frO + f2 / frO);
    const double nitro  = 0.1068  * std::exp(-3352.0 / T) / (frN + f2 / frN);
    const double relax = std::pow(tRatio, -2.5) * (oxygen + nitro);

    return 8.686 * f2 * (classical + relax);   // dB/m
}

//──────────────────────────────────────────────────────────────────────────
// Minimum-phase air-absorption filter.
//
// Building block: a Direct-Form biquad cascade (up to 3 sections), a0
// normalised to 1. Shelf topology = one first-order high-shelf (stored as a
// biquad with b2 = a2 = 0), fixed corner 5000 Hz, gain fit by a
// single-variable dB least-squares -- a deliberately loose "gross tilt".
// Cascade topology = three second-order RBJ high-shelf biquads at fixed
// corners {3000, 8000, 16000} Hz, gains fit by linearised dB least-squares
// -- a genuine multi-band staircase that tracks alpha*d tightly, including
// at large distance where alpha*d's rolloff accelerates (alpha ~ f^2)
// beyond what a first-order section can follow. Both are minimum-phase,
// zero-latency. The design bakes fs into the stored coefficients, so
// AirFilter needs no sample rate at all -- configure() just copies them.
//──────────────────────────────────────────────────────────────────────────

// ── biquad magnitude (dB) at frequency f, coeffs normalised so a0 = 1 ──────────
inline double biquadMagDb_(double b0, double b1, double b2, double a1, double a2,
                           double f, double fs) {
    const double w = 2.0 * M_PI * f / fs;
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = b0 + b1 * z1 + b2 * z2;
    const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
    return 20.0 * std::log10(std::abs(num / den));
}

// ── RBJ second-order high-shelf (Audio EQ Cookbook, S = 1), normalised a0 = 1 ──
// Unity (0 dB) at DC, `dbGain` dB at Nyquist, 12 dB/oct transition around fc.
// NOTE the digital-normalised angular frequency w0 = 2*pi*fc/fs here.
inline void designHiShelf2_(double fc, double dbGain, double fs,
                            double& b0, double& b1, double& b2,
                            double& a1, double& a2) {
    const double A  = std::pow(10.0, dbGain / 40.0);
    const double w0 = 2.0 * M_PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / 2.0 * std::sqrt(2.0);      // S = 1
    const double sq = std::sqrt(A);
    const double B0 =      A * ((A + 1) + (A - 1) * cw + 2 * sq * alpha);
    const double B1 = -2 * A * ((A - 1) + (A + 1) * cw);
    const double B2 =      A * ((A + 1) + (A - 1) * cw - 2 * sq * alpha);
    const double A0 =          (A + 1) - (A - 1) * cw + 2 * sq * alpha;
    const double A1 =      2 * ((A - 1) - (A + 1) * cw);
    const double A2 =          (A + 1) - (A - 1) * cw - 2 * sq * alpha;
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
}

// ── first-order high-shelf as a biquad (b2 = a2 = 0), prewarped bilinear ───────
// Unity at DC, `dbGain` dB at HF, 6 dB/oct transition at fc.
// NOTE w0 = 2*pi*fc is the ANALOG angular frequency here (NOT divided by fs);
// the sample rate enters only through the prewarp constant c. Do not conflate
// this with the digital w0 used in designHiShelf2_ above.
inline void designHiShelf1_(double fc, double dbGain, double fs,
                            double& b0, double& b1, double& a1) {
    const double V0 = std::pow(10.0, dbGain / 20.0);
    const double w0 = 2.0 * M_PI * fc;                   // analog rad/s
    const double c  = w0 / std::tan(w0 / (2.0 * fs));    // prewarp
    const double a0 = c + w0;
    b0 = (V0 * c + w0) / a0;
    b1 = (w0 - V0 * c) / a0;
    a1 = (w0 - c) / a0;
}

enum class Topology { Shelf = 0, Cascade = 1 };

struct AirFilterDesign {
    static constexpr int kMaxSections = 3;
    int    numSections = 0;
    // Per-section biquad coefficients, a0 normalised to 1 (first-order sections
    // set b2 = a2 = 0). fs is already baked in -- the runtime needs no sample rate.
    double b0[kMaxSections] = {1,1,1};
    double b1[kMaxSections] = {0,0,0};
    double b2[kMaxSections] = {0,0,0};
    double a1[kMaxSections] = {0,0,0};
    double a2[kMaxSections] = {0,0,0};
    double maxErrorDb = 0.0;
    bool   converged  = false;
};

// Fit the minimum-phase air filter to A(f) = -alpha*d (dB) over 20 Hz..min(20k,0.45fs).
inline AirFilterDesign designAirFilter(double dMeters, double tempC, double rhPercent,
                                       double fs, Topology topo, double paKPa = 101.325) {
    AirFilterDesign out;
    const int M = 48;
    const double fLo = 20.0, fHi = std::min(20000.0, 0.45 * fs);
    double freq[64], target[64];
    for (int i = 0; i < M; ++i) {
        const double t = (double)i / (double)(M - 1);
        freq[i]   = fLo * std::pow(fHi / fLo, t);
        target[i] = -alphaISO9613(freq[i], tempC, rhPercent, paKPa) * dMeters; // <= 0 dB
    }
    const double refDb = -6.0;   // basis reference gain for the linearised dB LSQ

    if (topo == Topology::Shelf) {
        // One first-order high-shelf at a fixed corner; fit its single gain.
        const double fc = 5000.0;
        double bn0, bn1, ba1;                 // unit-basis (refDb) coeffs
        designHiShelf1_(fc, refDb, fs, bn0, bn1, ba1);
        double num = 0.0, den = 0.0;
        for (int i = 0; i < M; ++i) {
            const double s = biquadMagDb_(bn0, bn1, 0.0, ba1, 0.0, freq[i], fs) / refDb; // dB per 1 dB gain
            num += s * target[i];
            den += s * s;
        }
        double gDb = den > 1e-12 ? num / den : 0.0;
        if (gDb > 0.0) gDb = 0.0;
        if (gDb < -60.0) gDb = -60.0;
        out.numSections = 1;
        designHiShelf1_(fc, gDb, fs, out.b0[0], out.b1[0], out.a1[0]);
        out.b2[0] = 0.0; out.a2[0] = 0.0;
    } else {
        // Three second-order high-shelves at fixed corners; fit their gains by
        // a linearised dB least-squares (basis = each section's dB shape per 1 dB
        // of gain; solve the 3x3 normal equations for the gain vector).
        const double C[3] = { 3000.0, 8000.0, 16000.0 };
        const int N = 3;
        double B[64][3];
        for (int k = 0; k < N; ++k) {
            double bb0, bb1, bb2, ba1, ba2;
            designHiShelf2_(C[k], refDb, fs, bb0, bb1, bb2, ba1, ba2);
            for (int i = 0; i < M; ++i)
                B[i][k] = biquadMagDb_(bb0, bb1, bb2, ba1, ba2, freq[i], fs) / refDb;
        }
        double A[3][3] = {{0}}, rhs[3] = {0}, g[3] = {0};
        for (int k = 0; k < N; ++k) {
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < M; ++i) A[k][j] += B[i][k] * B[i][j];
            for (int i = 0; i < M; ++i) rhs[k] += B[i][k] * target[i];
        }
        for (int p = 0; p < N; ++p) {                     // Gaussian elimination
            double piv = std::abs(A[p][p]) < 1e-12 ? 1e-12 : A[p][p];
            for (int r = p + 1; r < N; ++r) {
                const double m = A[r][p] / piv;
                for (int cc = p; cc < N; ++cc) A[r][cc] -= m * A[p][cc];
                rhs[r] -= m * rhs[p];
            }
        }
        for (int p = N - 1; p >= 0; --p) {
            double s = rhs[p];
            for (int cc = p + 1; cc < N; ++cc) s -= A[p][cc] * g[cc];
            g[p] = s / (std::abs(A[p][p]) < 1e-12 ? 1e-12 : A[p][p]);
        }
        out.numSections = N;
        for (int k = 0; k < N; ++k) {
            double gDb = g[k];
            if (gDb > 0.0) gDb = 0.0;
            if (gDb < -60.0) gDb = -60.0;
            designHiShelf2_(C[k], gDb, fs, out.b0[k], out.b1[k], out.b2[k], out.a1[k], out.a2[k]);
        }
    }

    // Achieved max dB error over the grid (true biquad-cascade magnitude).
    double err = 0.0;
    for (int i = 0; i < M; ++i) {
        double db = 0.0;
        for (int k = 0; k < out.numSections; ++k)
            db += biquadMagDb_(out.b0[k], out.b1[k], out.b2[k], out.a1[k], out.a2[k], freq[i], fs);
        err = std::max(err, std::abs(db - target[i]));
    }
    out.maxErrorDb = err;
    out.converged  = std::isfinite(err) && err < 8.0;  // covers both topologies
    return out;
}

// Runtime: shared biquad coefficients, per-instance state; one channel.
// Transposed Direct Form II per section (good numeric behaviour, one add/mul chain).
struct AirFilter {
    void configure(const AirFilterDesign& d) {
        n_ = d.numSections;
        for (int k = 0; k < n_; ++k) {
            b0_[k] = d.b0[k]; b1_[k] = d.b1[k]; b2_[k] = d.b2[k];
            a1_[k] = d.a1[k]; a2_[k] = d.a2[k];
        }
    }
    void reset() {
        for (int k = 0; k < AirFilterDesign::kMaxSections; ++k) { z1_[k] = 0.0; z2_[k] = 0.0; }
    }
    inline double process(double x) {
        double y = x;
        for (int k = 0; k < n_; ++k) {
            const double out = b0_[k] * y + z1_[k];
            z1_[k] = b1_[k] * y - a1_[k] * out + z2_[k];
            z2_[k] = b2_[k] * y - a2_[k] * out;
            y = out;
        }
        return y;
    }
private:
    int    n_ = 0;
    double b0_[AirFilterDesign::kMaxSections] = {1,1,1};
    double b1_[AirFilterDesign::kMaxSections] = {0,0,0};
    double b2_[AirFilterDesign::kMaxSections] = {0,0,0};
    double a1_[AirFilterDesign::kMaxSections] = {0,0,0};
    double a2_[AirFilterDesign::kMaxSections] = {0,0,0};
    double z1_[AirFilterDesign::kMaxSections] = {0,0,0};
    double z2_[AirFilterDesign::kMaxSections] = {0,0,0};
};

// Geometric 1/r spreading, attenuation-only, 1 m reference.
inline double spreadingGain(double dMeters) {
    const double dd = dMeters < 1.0 ? 1.0 : dMeters;
    return 1.0 / dd;
}

}} // namespace Seam::air
