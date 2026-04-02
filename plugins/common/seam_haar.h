//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Common · Haar wavelet spatial decomposition
//
// Giuseppe Silvi's method for creating a first-order AmbiX-like spatial
// representation from a mono signal, using the Haar wavelet transform.
//
// THEORY
// ──────
// The Haar wavelet is the simplest wavelet: a 2-point DFT applied
// sample-by-sample as a running filter:
//
//   sum(x) = (x[n] + x[n-1]) / 2    — lowpass  (DC component)
//   dif(x) = (x[n] - x[n-1]) / 2    — highpass (AC component)
//
// These form a Quadrature Mirror Filter (QMF) pair. Applied recursively
// as a filter bank, they decompose the signal into frequency-ordered
// spatial components:
//
//   Level 0: input signal
//   Level 1: dft2 → (sum, dif)
//   Level 2: dft2 on sum → (sum_of_sum, dif_of_sum), pass dif
//
// haarmn(1) creates 4 outputs from 1 input:
//   qmf(2) → 3 signals + 1 passthrough, then reversed by ro.cross(4)
//
// The 4 resulting signals are treated as first-order AmbiX components
// and can be rotated with rotateYPR.
//
// FAUST REFERENCE (haarmn from test2/LR2XHGR.dsp):
//   dft2(x) = ((x+x')/2, (x-x')/2);
//   qmf(0) = _; qmf(1) = dft2; qmf(N) = dft2 : qmf(N-1), _;
//   haarmn(0) = _; haarmn(N) = _ <: qmf((N+1)^2-2), _;
//   m2xhgr = haarmn(1) : ro.cross(4);
//─────────────────────────────────────────────────────────────────────────────

#pragma once

namespace Seam {

// State for the Haar mono-to-4ch decomposition.
// Needs 2 delay elements: one for the first dft2, one for the second.
template <typename T>
struct HaarState
{
    T z1 = 0;  // delay for first dft2 (input → sum/dif)
    T z2 = 0;  // delay for second dft2 (sum → sum_of_sum/dif_of_sum)

    void reset ()
    {
        z1 = 0;
        z2 = 0;
    }
};

// Process one sample through haarmn(1) : ro.cross(4).
//
// Input:  x (mono sample)
// Output: a0, a1, a2, a3 (4 spatial components)
//
// After ro.cross(4), the order is reversed from the Faust output:
//   a0 = passthrough (original signal)
//   a1 = dif  (first-level difference)
//   a2 = dif_of_sum (second-level difference)
//   a3 = sum_of_sum (second-level sum)
template <typename T>
inline void haarDecompose (HaarState<T>& state, T x,
                           T& a0, T& a1, T& a2, T& a3)
{
    // First dft2: running sum and difference of input
    T sum0 = (x + state.z1) * static_cast<T> (0.5);
    T dif0 = (x - state.z1) * static_cast<T> (0.5);
    state.z1 = x;

    // Second dft2: applied to sum0
    T sum1 = (sum0 + state.z2) * static_cast<T> (0.5);
    T dif1 = (sum0 - state.z2) * static_cast<T> (0.5);
    state.z2 = sum0;

    // qmf(2) outputs: [sum1, dif1, dif0]  (3 signals)
    // haarmn(1) adds passthrough: [sum1, dif1, dif0, x]  (4 signals)
    // ro.cross(4) reverses: [x, dif0, dif1, sum1]
    a0 = x;
    a1 = dif0;
    a2 = dif1;
    a3 = sum1;
}

} // namespace Seam
