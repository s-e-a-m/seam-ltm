// SEAM-LTM · seam_fft — SDK-free radix-2 FFT (shared analysis primitive).
//
// In-place Cooley-Tukey radix-2 DIT on interleaved complex pairs. Adapted from
// GS's the-accountant/src/analysis/fft.hpp. Reused by strx (Welch spectrum) and,
// later, the STONE transfer-function measurement (Spec 3).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

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

// Streaming Welch power-spectrum accumulator: Hann window, 50% overlap, live
// EMA time average. Allocation-free after prepare().
class Welch {
public:
    void prepare(int fftSize, double emaTau, double fs) {
        n_ = fftSize; bins_ = fftSize/2 + 1; hop_ = fftSize/2;
        fs_ = (fs > 0.0) ? fs : 48000.0;
        // EMA coefficient over the hop rate (one hop = hop_ samples).
        const double dt = double(hop_) / fs_;
        ema_ = (emaTau > 0.0) ? std::exp(-dt / emaTau) : 0.0;
        win_.assign(n_, 0.0f);
        double wsum = 0.0;
        for (int i = 0; i < n_; ++i) {                 // Hann
            win_[i] = 0.5f * (1.0f - std::cos(2.0*M_PI*i/(n_-1)));
            wsum += win_[i]*win_[i];
        }
        winNorm_ = 1.0 / (wsum > 0.0 ? wsum : 1.0);    // power normalization
        ring_.assign(n_, 0.0f);
        scratch_.assign(2*n_, 0.0f);
        magDb_.assign(bins_, -120.0f);
        magLin_.assign(bins_, 0.0f);
        reset();
    }
    void reset() {
        pos_ = 0; sinceHop_ = 0; primed_ = false; newFrame_ = false;
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        std::fill(magLin_.begin(), magLin_.end(), 0.0f);
        std::fill(magDb_.begin(), magDb_.end(), -120.0f);
    }
    void push(float x) {
        ring_[pos_] = x;
        pos_ = (pos_ + 1) % n_;
        if (++sinceHop_ >= hop_) { sinceHop_ = 0; runFrame(); }
    }
    bool hasNewFrame() { bool f = newFrame_; newFrame_ = false; return f; }
    const float* magnitudeDb() const { return magDb_.data(); }
    int numBins() const { return bins_; }
private:
    void runFrame() {
        // windowed frame from the ring (oldest sample = current pos_)
        for (int i = 0; i < n_; ++i) {
            const float s = ring_[(pos_ + i) % n_];
            scratch_[2*i]   = s * win_[i];
            scratch_[2*i+1] = 0.0f;
        }
        transform(scratch_.data(), n_, true);
        for (int k = 0; k < bins_; ++k) {
            const double re = scratch_[2*k], im = scratch_[2*k+1];
            const double p = (re*re + im*im) * winNorm_;   // power
            magLin_[k] = float(p + ema_ * (magLin_[k] - p)); // EMA on power
            const double db = 10.0 * std::log10(magLin_[k] > 1e-12 ? magLin_[k] : 1e-12);
            magDb_[k] = float(db < -120.0 ? -120.0 : db);
        }
        newFrame_ = true;
    }
    int n_=0, bins_=0, hop_=0, pos_=0, sinceHop_=0;
    double fs_=48000.0, ema_=0.0, winNorm_=1.0;
    bool primed_=false, newFrame_=false;
    std::vector<float> win_, ring_, scratch_, magLin_, magDb_;
};

}} // namespace seam::fft
