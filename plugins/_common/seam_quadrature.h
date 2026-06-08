//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · seam_quadrature — wideband 90° quadrature design engine
//
// Designs an all-pass quadrature pair (H_R, H_I) whose phase difference holds
// −90° across a band, by a minimax phase fit at the actual sample rate.
//
// PYTHON REFERENCE: plugins/x2uhj/tools/design_quadrature_perfs.py
// FAUST REFERENCE (seam.ambisonics.lib): the UHJ quadrature pair.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <complex>

namespace Seam { namespace quadrature {

struct APSpec { double f; double Q; };

// Coefficients (a1, a2) of one RBJ all-pass biquad at (f, Q, fs).
inline void allpassCoeffs(double f, double Q, double fs, double& a1, double& a2) {
    if (Q < 1e-6) Q = 1e-6; // bilinear coefficients require a positive Q
    const double w0 = 2.0 * M_PI * f / fs;
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double n = 1.0 + alpha;
    a1 = -2.0 * std::cos(w0) / n;
    a2 = (1.0 - alpha) / n;
}

// Complex response of one all-pass biquad at digital angular frequency w.
inline std::complex<double> allpassSectionResponse(double f, double Q, double fs, double w) {
    double a1, a2;
    allpassCoeffs(f, Q, fs, a1, a2);
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = a2 + a1 * z1 + z2;
    const std::complex<double> den = 1.0 + a1 * z1 + a2 * z2;
    return num / den;
}

inline double allpassSectionMag(double f, double Q, double fs, double w) {
    return std::abs(allpassSectionResponse(f, Q, fs, w));
}

inline double allpassSectionPhase(double f, double Q, double fs, double w) {
    return std::arg(allpassSectionResponse(f, Q, fs, w));
}

// Cascade phase: sum of per-section unwrapped phases across the frequency grid.
// Matches numpy's per-section unwrap-then-sum in design_quadrature_perfs.py.
inline void cascadePhase(const APSpec* secs, int n, double fs,
                         const double* freqs, int M, double* out) {
    for (int i = 0; i < M; ++i) out[i] = 0.0;
    for (int s = 0; s < n; ++s) {
        double prev = 0.0, offset = 0.0;
        for (int i = 0; i < M; ++i) {
            const double w = 2.0 * M_PI * freqs[i] / fs;
            double ph = allpassSectionPhase(secs[s].f, secs[s].Q, fs, w);
            if (i > 0) {
                double d = ph - prev;
                while (d >  M_PI) { offset -= 2.0 * M_PI; d -= 2.0 * M_PI; }
                while (d < -M_PI) { offset += 2.0 * M_PI; d += 2.0 * M_PI; }
            }
            prev = ph;
            out[i] += ph + offset;
        }
    }
}

struct QuadratureDesign {
    static constexpr int kMaxSections = 8;
    APSpec hr[kMaxSections];
    APSpec hi[kMaxSections];
    int    nSections = 0;
    double maxErrorDeg = 0.0;
    double sampleRate = 0.0; // fs the design was computed at, for readout/UI
    bool   converged = false;
};

namespace detail {

// Solve A x = b for an n x n system (Gaussian elimination, partial pivoting).
inline bool solveLinear(double* A, double* b, int n) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (std::fabs(A[r*n+col]) > std::fabs(A[piv*n+col])) piv = r;
        if (std::fabs(A[piv*n+col]) < 1e-18) return false;
        if (piv != col) {
            for (int c = 0; c < n; ++c) std::swap(A[piv*n+c], A[col*n+c]);
            std::swap(b[piv], b[col]);
        }
        for (int r = 0; r < n; ++r) {
            if (r == col) continue;
            const double m = A[r*n+col] / A[col*n+col];
            for (int c = col; c < n; ++c) A[r*n+c] -= m * A[col*n+c];
            b[r] -= m * b[col];
        }
    }
    for (int i = 0; i < n; ++i) b[i] /= A[i*n+i];
    return true;
}

inline void unpack(const double* x, int nSec, APSpec* hr, APSpec* hi) {
    for (int i = 0; i < nSec; ++i) { hr[i].f = x[2*i]; hr[i].Q = x[2*i+1]; }
    const int off = 2 * nSec;
    for (int i = 0; i < nSec; ++i) { hi[i].f = x[off+2*i]; hi[i].Q = x[off+2*i+1]; }
}

inline void residuals(const double* x, int nSec, double fs,
                      const double* freqs, int M, double* r) {
    APSpec hr[QuadratureDesign::kMaxSections], hi[QuadratureDesign::kMaxSections];
    unpack(x, nSec, hr, hi);
    double pr[1024], pi[1024];
    cascadePhase(hr, nSec, fs, freqs, M, pr);
    cascadePhase(hi, nSec, fs, freqs, M, pi);
    const double target = -M_PI / 2.0;
    for (int i = 0; i < M; ++i) r[i] = pi[i] - pr[i] - target;
}

inline double sumSquares(const double* r, int M) {
    double s = 0.0; for (int i = 0; i < M; ++i) s += r[i] * r[i]; return s;
}
inline double maxAbsDeg(const double* r, int M) {
    double m = 0.0; for (int i = 0; i < M; ++i) m = std::fmax(m, std::fabs(r[i]));
    return m * 180.0 / M_PI;
}

} // namespace detail

inline QuadratureDesign designQuadrature(double fs, double fLo, double fHi, int nSections) {
    QuadratureDesign d;
    if (nSections < 1) nSections = 1;
    if (nSections > QuadratureDesign::kMaxSections) nSections = QuadratureDesign::kMaxSections;
    d.nSections = nSections;
    d.sampleRate = fs;

    const int N = 4 * nSections;
    const int M = 512;
    double freqs[1024];
    for (int i = 0; i < M; ++i)
        freqs[i] = fLo * std::pow(fHi / fLo, double(i) / (M - 1));

    double x[4 * QuadratureDesign::kMaxSections];
    double lo[4 * QuadratureDesign::kMaxSections];
    double hi[4 * QuadratureDesign::kMaxSections];
    if (nSections == 3) {
        const double seed[12] = {141.9,0.2019, 671.7,0.2122, 18654.0,0.3031,
                                 24.0,0.3090, 2992.0,0.3848, 3220.0,0.0963};
        for (int i = 0; i < 12; ++i) x[i] = seed[i];
    } else {
        for (int i = 0; i < nSections; ++i) {
            const double t = double(i) / std::max(1, nSections - 1);
            x[2*i]   = fLo * 2.0 * std::pow(fHi*0.6/(fLo*2.0), t);
            x[2*i+1] = 0.3;
            x[2*nSections+2*i]   = fLo * std::pow(fHi*0.4/fLo, t);
            x[2*nSections+2*i+1] = 0.3;
        }
    }
    for (int i = 0; i < N; i += 2) { lo[i]=10.0; hi[i]=fs/2.0-1.0; lo[i+1]=0.01; hi[i+1]=5.0; }

    double r[1024], rPert[1024];
    static double J[1024 * (4 * QuadratureDesign::kMaxSections)];
    double lambda = 1e-3;
    detail::residuals(x, nSections, fs, freqs, M, r);
    double cost = detail::sumSquares(r, M);
    for (int iter = 0; iter < 200; ++iter) {
        for (int j = 0; j < N; ++j) {
            const double h = 1e-6 * std::fmax(1.0, std::fabs(x[j]));
            const double save = x[j];
            x[j] = save + h;
            detail::residuals(x, nSections, fs, freqs, M, rPert);
            x[j] = save;
            for (int i = 0; i < M; ++i) J[i*N + j] = (rPert[i] - r[i]) / h;
        }
        double A[(4*QuadratureDesign::kMaxSections)*(4*QuadratureDesign::kMaxSections)];
        double g[4 * QuadratureDesign::kMaxSections];
        for (int a = 0; a < N; ++a) {
            g[a] = 0.0;
            for (int i = 0; i < M; ++i) g[a] += J[i*N+a] * r[i];
            for (int b = 0; b < N; ++b) {
                double s = 0.0;
                for (int i = 0; i < M; ++i) s += J[i*N+a] * J[i*N+b];
                A[a*N+b] = s;
            }
        }
        for (int a = 0; a < N; ++a) A[a*N+a] += lambda * A[a*N+a];
        double dx[4 * QuadratureDesign::kMaxSections];
        for (int a = 0; a < N; ++a) dx[a] = -g[a];
        if (!detail::solveLinear(A, dx, N)) { lambda *= 4.0; continue; }
        double xNew[4 * QuadratureDesign::kMaxSections];
        for (int a = 0; a < N; ++a) {
            xNew[a] = x[a] + dx[a];
            if (xNew[a] < lo[a]) xNew[a] = lo[a];
            if (xNew[a] > hi[a]) xNew[a] = hi[a];
        }
        detail::residuals(xNew, nSections, fs, freqs, M, rPert);
        const double costNew = detail::sumSquares(rPert, M);
        if (costNew < cost) {
            for (int a = 0; a < N; ++a) x[a] = xNew[a];
            for (int i = 0; i < M; ++i) r[i] = rPert[i];
            const double improve = cost - costNew;
            cost = costNew;
            lambda = std::fmax(lambda * 0.5, 1e-12);
            if (improve < 1e-12) break;
        } else {
            lambda *= 4.0;
            if (lambda > 1e12) break;
        }
    }

    detail::residuals(x, nSections, fs, freqs, M, r);
    d.maxErrorDeg = detail::maxAbsDeg(r, M);
    detail::unpack(x, nSections, d.hr, d.hi);
    d.converged = (d.maxErrorDeg < 10.0);
    return d;
}

}} // namespace Seam::quadrature
