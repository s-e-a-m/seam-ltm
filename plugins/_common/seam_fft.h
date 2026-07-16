// SEAM-LTM · seam_fft — SDK-free radix-2 FFT (shared analysis primitive).
//
// In-place Cooley-Tukey radix-2 DIT on interleaved complex pairs. Adapted from
// GS's the-accountant/src/analysis/fft.hpp. Reused by strx (Welch spectrum) and,
// later, the STONE transfer-function measurement (Spec 3).
#pragma once
#include <cmath>

namespace seam { namespace fft {

// data: n interleaved complex pairs (re,im,...), length 2*n. n must be a power
// of two. No 1/n scaling. forward=true → e^{-i2pi kn/N}.
inline void transform(float* data, int n, bool forward) {
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = data[2*i],   ti = data[2*i+1];
            data[2*i]   = data[2*j];   data[2*i+1] = data[2*j+1];
            data[2*j]   = tr;          data[2*j+1] = ti;
        }
    }
    const float sign = forward ? -1.0f : 1.0f;
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = sign * 2.0f * float(M_PI) / len;
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;              // running twiddle
            for (int k = 0; k < len/2; ++k) {
                const int a = 2*(i+k), b = 2*(i+k+len/2);
                const float ur = data[a],   ui = data[a+1];
                const float vr = data[b]*cr - data[b+1]*ci;
                const float vi = data[b]*ci + data[b+1]*cr;
                data[a]   = ur + vr;  data[a+1] = ui + vi;
                data[b]   = ur - vr;  data[b+1] = ui - vi;
                const float ncr = cr*wr - ci*wi;     // advance twiddle
                ci = cr*wi + ci*wr;  cr = ncr;
            }
        }
    }
}

}} // namespace seam::fft
