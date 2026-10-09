//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_spectrum.h — N live spectra, audio thread to GUI (SDK-free)
//
// FAUST REFERENCE: none in the SEAM libraries; an.mth_octave_spectral_level
// (analyzers.lib) is the idiomatic SR-independent equivalent of a display
// of this kind. This is the measuring half of strx's spectrum, made generic:
// one seam::fft::Welch per channel (Hann, 50% overlap, EMA on power, a full-
// scale sine reads 0 dB at its bin) and strx's triple buffer (strx_dsp.h),
// so a whole frame changes owner in a single atomic exchange.
//
// Two choices strx does not need:
// * the FFT grows with the rate (spectrumFftSize): 4096 points at 44.1/48
//   kHz, 8192 at 88.2/96, 16384 at 176.4/192, so a bin is 10.8-11.7 Hz wide
//   at every rate and the low end reads the same at 96 kHz as at 48;
// * the channels' hops are staggered by hop/N, so the N FFTs fall on N
//   different samples instead of piling onto one block.
//
// prepare() allocates (setActive); process() and tryRead() do not.
// Written 2026-10-09 for stunedrev (the four lines, live in rehearsal).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_fft.h"
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace Seam {

// Points of the FFT at fs: 4096 per 48 kHz, rounded to a power of two.
inline int spectrumFftSize(double fs) {
    int n = 4096;
    while (fs / n > 12.0 && n < 16384) n *= 2;
    return n;
}

template <int N>
struct SpectrumFrame {
    static constexpr int kMaxBins = 16384 / 2 + 1;   // the 192 kHz FFT
    int   numBins = 0;                               // 0 until the first frame
    float db[N][kMaxBins] = {};                      // power per bin, dBFS, floor -120
};

template <int N>
class MultiSpectrum {
public:
    // Outside the audio thread. tau: the EMA's time constant, s.
    void prepare(double fs, double tau) {
        prepared_ = false;
        const int n = spectrumFftSize(fs);
        for (int c = 0; c < N; ++c) welch_[c].prepare(n, tau, fs, c * (n / 2) / N);
        bins_ = n / 2 + 1;
        ffts_ = 0;
        back_.store(0); writeBuf_ = 1; front_ = 2;
        for (auto& s : slots_) s.numBins = 0;
        sampleRate_.store(fs);
        prepared_ = true;
    }

    void release() { prepared_ = false; sampleRate_.store(0.0); }

    // Audio thread: N channels of n samples. A frame is published at the
    // end of any block in which one of the channels completed a hop.
    template <class T>
    void process(const T* const* x, int n) {
        if (!prepared_) return;
        bool fresh = false;
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < N; ++c) {
                welch_[c].push((float)x[c][i]);
                if (welch_[c].hasNewFrame()) { fresh = true; ++ffts_; }
            }
        if (!fresh) return;
        SpectrumFrame<N>& fr = slots_[writeBuf_];
        fr.numBins = bins_;
        for (int c = 0; c < N; ++c)
            std::copy(welch_[c].magnitudeDb(), welch_[c].magnitudeDb() + bins_, fr.db[c]);
        const uint8_t old = back_.exchange(uint8_t(writeBuf_ | kDirty), std::memory_order_acq_rel);
        writeBuf_ = old & kIdxMask;
    }

    // GUI thread, a single consumer: copies the newest frame into out, or
    // returns false when nothing was published since the last read.
    bool tryRead(SpectrumFrame<N>& out) {
        if ((back_.load(std::memory_order_acquire) & kDirty) == 0) return false;
        const uint8_t old = back_.exchange(front_, std::memory_order_acq_rel);
        front_ = old & kIdxMask;
        const SpectrumFrame<N>& src = slots_[front_];
        out.numBins = src.numBins;
        for (int c = 0; c < N; ++c) std::copy(src.db[c], src.db[c] + src.numBins, out.db[c]);
        return true;
    }

    long   fftCount() const   { return ffts_; }
    double sampleRate() const { return sampleRate_.load(); }

private:
    static constexpr uint8_t kIdxMask = 0x03;
    static constexpr uint8_t kDirty   = 0x04;
    seam::fft::Welch welch_[N];
    int  bins_ = 0;
    long ffts_ = 0;
    bool prepared_ = false;
    SpectrumFrame<N> slots_[3];
    std::atomic<uint8_t> back_{0};
    uint8_t writeBuf_ = 1, front_ = 2;
    std::atomic<double> sampleRate_{0.0};
};

} // namespace Seam
