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

//──────────────────────────────────────────────────────────────────────────
// Polyphase first-order all-pass section (Niemitalo topology).
// One section realises H(z) = (a² − z⁻²)/(1 − a² z⁻²): a single coefficient,
// one multiply per sample. Used by the polyphase quadrature designer.
//──────────────────────────────────────────────────────────────────────────

// Complex response of one polyphase section at digital angular frequency w.
inline std::complex<double> polyphaseSectionResponse(double a, double w) {
    const double a2 = a * a;
    const std::complex<double> z2 = std::exp(std::complex<double>(0.0, -2.0 * w));
    const std::complex<double> num = a2 - z2;
    const std::complex<double> den = 1.0 - a2 * z2;
    return num / den;
}

inline double polyphaseSectionMag(double a, double w) {
    return std::abs(polyphaseSectionResponse(a, w));
}

inline double polyphaseSectionPhase(double a, double w) {
    return std::arg(polyphaseSectionResponse(a, w));
}

// Path phase: sum of per-section unwrapped polyphase phases across the grid.
// Matches numpy's per-section unwrap-then-sum in topo_polyphase.py.
inline void polyphaseCascadePhase(const double* coeffs, int n, double fs,
                                  const double* freqs, int M, double* out) {
    for (int i = 0; i < M; ++i) out[i] = 0.0;
    for (int s = 0; s < n; ++s) {
        double prev = 0.0, offset = 0.0;
        for (int i = 0; i < M; ++i) {
            const double w = 2.0 * M_PI * freqs[i] / fs;
            double ph = polyphaseSectionPhase(coeffs[s], w);
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

struct PolyphaseDesign {
    static constexpr int kMaxPerPath = 8;
    double a[2 * kMaxPerPath] = {0}; // path A coeffs then path B coeffs
    int    nA = 0;
    int    nB = 0;
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

// Box-constrained Levenberg-Marquardt least-squares solver, shared by the
// designers. `residual(x, r)` fills M residuals from the N-parameter vector x;
// x is refined in place. Jacobian by forward differences; LM damping adapts.
static constexpr int kMaxParams = 4 * QuadratureDesign::kMaxSections;
template <typename ResidualFn>
inline void solveLM(double* x, int N, const double* lo, const double* hi,
                    int M, const ResidualFn& residual) {
    double r[1024], rPert[1024];
    static double J[1024 * kMaxParams];
    double lambda = 1e-3;
    residual(x, r);
    double cost = sumSquares(r, M);
    for (int iter = 0; iter < 200; ++iter) {
        for (int j = 0; j < N; ++j) {
            const double h = 1e-6 * std::fmax(1.0, std::fabs(x[j]));
            const double save = x[j];
            x[j] = save + h;
            residual(x, rPert);
            x[j] = save;
            for (int i = 0; i < M; ++i) J[i*N + j] = (rPert[i] - r[i]) / h;
        }
        double A[kMaxParams*kMaxParams];
        double g[kMaxParams];
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
        double dx[kMaxParams];
        for (int a = 0; a < N; ++a) dx[a] = -g[a];
        if (!solveLinear(A, dx, N)) { lambda *= 4.0; continue; }
        double xNew[kMaxParams];
        for (int a = 0; a < N; ++a) {
            xNew[a] = x[a] + dx[a];
            if (xNew[a] < lo[a]) xNew[a] = lo[a];
            if (xNew[a] > hi[a]) xNew[a] = hi[a];
        }
        residual(xNew, rPert);
        const double costNew = sumSquares(rPert, M);
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
}

// Residual for the polyphase pair: x = [path A coeffs | path B coeffs], each
// `order` long. Path B carries a one-sample relative delay (z⁻¹).
inline void polyphaseResiduals(const double* x, int order, double fs,
                               const double* freqs, int M, double* r) {
    double pa[1024], pb[1024];
    polyphaseCascadePhase(x,         order, fs, freqs, M, pa);
    polyphaseCascadePhase(x + order, order, fs, freqs, M, pb);
    const double target = -M_PI / 2.0;
    for (int i = 0; i < M; ++i) {
        const double w = 2.0 * M_PI * freqs[i] / fs;
        r[i] = (pb[i] - w) - pa[i] - target;
    }
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

    detail::solveLM(x, N, lo, hi, M,
        [&](const double* xx, double* rr) {
            detail::residuals(xx, nSections, fs, freqs, M, rr);
        });

    double r[1024];
    detail::residuals(x, nSections, fs, freqs, M, r);
    d.maxErrorDeg = detail::maxAbsDeg(r, M);
    detail::unpack(x, nSections, d.hr, d.hi);
    d.converged = (d.maxErrorDeg < 10.0);
    return d;
}

// Design a polyphase first-order quadrature pair live at sample rate `fs`.
// `order` is the number of sections per path. Seeded from the published
// Niemitalo coefficients and re-optimised by the shared LM core, so accuracy
// holds across sample rates where the fixed coefficients would degrade.
inline PolyphaseDesign designPolyphase(double fs, int order) {
    PolyphaseDesign d;
    if (order < 1) order = 1;
    if (order > PolyphaseDesign::kMaxPerPath) order = PolyphaseDesign::kMaxPerPath;
    d.nA = order; d.nB = order; d.sampleRate = fs;

    const int N = 2 * order;
    const int M = 512;
    const double fLo = 20.0, fHi = 20000.0;
    double freqs[1024];
    for (int i = 0; i < M; ++i)
        freqs[i] = fLo * std::pow(fHi / fLo, double(i) / (M - 1));

    // Published Niemitalo order-4 coefficients (as shipped in WigWare).
    static const double FIXED_A[4] = {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737};
    static const double FIXED_B[4] = {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278};

    double x[2 * PolyphaseDesign::kMaxPerPath];
    double lo[2 * PolyphaseDesign::kMaxPerPath];
    double hi[2 * PolyphaseDesign::kMaxPerPath];
    for (int i = 0; i < order; ++i) {
        x[i]         = (i < 4) ? FIXED_A[i] : 0.5 + 0.4 * double(i) / order; // path A
        x[order + i] = (i < 4) ? FIXED_B[i] : 0.5 + 0.4 * double(i) / order; // path B
    }
    for (int i = 0; i < N; ++i) { lo[i] = 0.01; hi[i] = 0.999999; }

    detail::solveLM(x, N, lo, hi, M,
        [&](const double* xx, double* rr) {
            detail::polyphaseResiduals(xx, order, fs, freqs, M, rr);
        });

    double r[1024];
    detail::polyphaseResiduals(x, order, fs, freqs, M, r);
    d.maxErrorDeg = detail::maxAbsDeg(r, M);
    for (int i = 0; i < N; ++i) d.a[i] = x[i];
    d.converged = (d.maxErrorDeg < 10.0);
    return d;
}

//──────────────────────────────────────────────────────────────────────────
// QuadraturePair — runtime processor for a designed quadrature pair.
//
// Configured from either a QuadratureDesign (two RBJ biquad all-pass cascades)
// or a PolyphaseDesign (two first-order all-pass paths, one-sample delay on the
// quadrature path). process() returns the in-phase and quadrature outputs whose
// phase difference holds −90° across the band.
//──────────────────────────────────────────────────────────────────────────
class QuadraturePair {
public:
    static constexpr int kMaxSections = 8;

    void configureRBJ(const QuadratureDesign& d) {
        mode_ = Mode::RBJ;
        nA_ = d.nSections; nB_ = d.nSections;
        for (int i = 0; i < nA_; ++i) {
            double a1, a2;
            allpassCoeffs(d.hr[i].f, d.hr[i].Q, d.sampleRate, a1, a2);
            pathA_[i] = makeAllpassBiquad(a1, a2);
            allpassCoeffs(d.hi[i].f, d.hi[i].Q, d.sampleRate, a1, a2);
            pathB_[i] = makeAllpassBiquad(a1, a2);
        }
        reset();
    }

    void configurePolyphase(const PolyphaseDesign& d) {
        mode_ = Mode::Polyphase;
        nA_ = d.nA; nB_ = d.nB;
        for (int i = 0; i < nA_; ++i) pathA_[i] = makePolyphaseBiquad(d.a[i]);
        for (int i = 0; i < nB_; ++i) pathB_[i] = makePolyphaseBiquad(d.a[d.nA + i]);
        reset();
    }

    void reset() {
        for (int i = 0; i < kMaxSections; ++i) { pathA_[i].reset(); pathB_[i].reset(); }
        delayB_ = 0.0;
    }

    // x → (inPhase, quad); quad lags inPhase by 90° across the band.
    inline void process(double x, double& inPhase, double& quad) {
        double a = x; for (int i = 0; i < nA_; ++i) a = pathA_[i].process(a);
        double b = x; for (int i = 0; i < nB_; ++i) b = pathB_[i].process(b);
        inPhase = a;
        if (mode_ == Mode::Polyphase) { quad = delayB_; delayB_ = b; } // z⁻¹ on B
        else                          { quad = b; }
    }

private:
    // Transposed-free Direct-Form-I biquad: y = b0 x + b1 x1 + b2 x2 − a1 y1 − a2 y2.
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline double process(double x) {
            double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void reset() { x1 = x2 = y1 = y2 = 0; }
    };

    // RBJ all-pass: H(z) = (a2 + a1 z⁻¹ + z⁻²)/(1 + a1 z⁻¹ + a2 z⁻²).
    static Biquad makeAllpassBiquad(double a1, double a2) {
        Biquad bq; bq.b0 = a2; bq.b1 = a1; bq.b2 = 1.0; bq.a1 = a1; bq.a2 = a2;
        return bq;
    }
    // Polyphase first-order all-pass: H(z) = (a² − z⁻²)/(1 − a² z⁻²).
    static Biquad makePolyphaseBiquad(double a) {
        const double a2 = a * a;
        Biquad bq; bq.b0 = a2; bq.b1 = 0.0; bq.b2 = -1.0; bq.a1 = 0.0; bq.a2 = -a2;
        return bq;
    }

    enum class Mode { RBJ, Polyphase } mode_ = Mode::RBJ;
    Biquad pathA_[kMaxSections];
    Biquad pathB_[kMaxSections];
    int nA_ = 0, nB_ = 0;
    double delayB_ = 0.0;
};

}} // namespace Seam::quadrature
