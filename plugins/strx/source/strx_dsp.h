// SEAM-LTM · strx_dsp — SDK-free analysis core for the STONE observation analyzer.
//
// FAUST REFERENCE:
//   seam.stereophony.lib : sst.sdmx   — Blumlein M/S sum-and-difference matrix
//   seam.analyzers.lib   : san.correlation, san.width, san.panorama, san.vectorangle
//   seam.analyzers.lib   : an.mth_octave_spectral_level (idiomatic spectrum equiv.)
// The spectral curve here uses a Welch FFT (seam_fft.h) for a finer display,
// citing the filterbank analyzer as the SR-independent Faust equivalent.
#pragma once
#include "seam_fft.h"
#include "seam_meter.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Seam { namespace strx {

struct AnalysisFrame {
    float inL = -60.f, inR = -60.f, mid = -60.f, side = -60.f;
    float correlation = 0.f;   // [-1,1]
    float width       = 0.f;   // [0,1], 0 = mono
    float panorama    = 0.f;   // [-1,1]
    float angleRad    = 0.f;

    static constexpr int kMaxPoints = 1024;
    int numPoints = 0;
    float gx[kMaxPoints] = {};  // goniometer x = S, normalized ~[-1,1]
    float gy[kMaxPoints] = {};  // goniometer y = M, normalized ~[-1,1]

    static constexpr int kNumBins = 2049;  // kFftSize/2 + 1, kFftSize = 4096
    float specM[kNumBins] = {};  // dB
    float specS[kNumBins] = {};  // dB
    int numBins = 0;
};

class Analyzer {
public:
    void prepare(double fs) {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        lvlL_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlR_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlM_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlS_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        // one-pole for the covariance means (0.3 s, matching san sfa_tau)
        coef_ = std::exp(-1.0 / (0.3 * fs_));
        welchM_.prepare(kFftSize, 2.0, fs_);   // τ = 2 s live EMA
        welchS_.prepare(kFftSize, 2.0, fs_);
        reset();
    }
    void reset() {
        lvlL_.reset(); lvlR_.reset(); lvlM_.reset(); lvlS_.reset();
        mLR_ = mLL_ = mRR_ = 0.0;
        welchM_.reset(); welchS_.reset();
        for (auto& s : slots_) s = AnalysisFrame{};
        writeSlot_ = 0;
        ready_.store(-1, std::memory_order_relaxed);
        reading_.store(-1, std::memory_order_relaxed);
        lastRead_ = -1;
    }
    void analyzeScalars(const float* L, const float* R, int n) {
        AnalysisFrame& fr = slots_[writeSlot_];
        double rmsL=0, rmsR=0, rmsM=0, rmsS=0;
        for (int i = 0; i < n; ++i) {
            const double l = L[i], r = R[i];
            const double m = (l + r) * 0.70710678, s = (l - r) * 0.70710678; // sst.sdmx
            rmsL = lvlL_.feed(float(l)); rmsR = lvlR_.feed(float(r));
            rmsM = lvlM_.feed(float(m)); rmsS = lvlS_.feed(float(s));
            mLR_ = l*r + coef_*(mLR_ - l*r);   // moving means (san.correlation)
            mLL_ = l*l + coef_*(mLL_ - l*l);
            mRR_ = r*r + coef_*(mRR_ - r*r);
        }
        const double eps = 1e-12;
        fr.inL = float(seam::meter::lin2db(rmsL));
        fr.inR = float(seam::meter::lin2db(rmsR));
        fr.mid = float(seam::meter::lin2db(rmsM));
        fr.side = float(seam::meter::lin2db(rmsS));
        fr.correlation = float(mLR_ / std::sqrt(std::max(eps, mLL_*mRR_)));
        fr.width = float(rmsS / std::max(eps, rmsM + rmsS));    // san.width
        fr.panorama = float((mRR_ - mLL_) / std::max(eps, mRR_ + mLL_)); // san.panorama
        fr.angleRad = float(0.5 * std::atan2(2.0*mLR_, mLL_ - mRR_));     // san.vectorangle
    }
    int fftSize() const { return kFftSize; }

    void analyze(const float* L, const float* R, int n) {
        analyzeScalars(L, R, n);
        AnalysisFrame& fr = slots_[writeSlot_];   // same slot analyzeScalars() just filled
        // Goniometer: (x=S, y=M), decimate by stride to <= kMaxPoints.
        const int stride = (n + AnalysisFrame::kMaxPoints - 1) / AnalysisFrame::kMaxPoints;
        int p = 0;
        for (int i = 0; i < n && p < AnalysisFrame::kMaxPoints; i += (stride > 0 ? stride : 1)) {
            const float s = (L[i] - R[i]) * 0.70710678f;
            const float m = (L[i] + R[i]) * 0.70710678f;
            fr.gx[p] = s; fr.gy[p] = m; ++p;
        }
        fr.numPoints = p;
        // Spectra: feed both Welch analyzers per sample.
        for (int i = 0; i < n; ++i) {
            const float m = (L[i] + R[i]) * 0.70710678f;
            const float s = (L[i] - R[i]) * 0.70710678f;
            welchM_.push(m); welchS_.push(s);
        }
        fr.numBins = welchM_.numBins();
        const float* mM = welchM_.magnitudeDb();
        const float* mS = welchS_.magnitudeDb();
        for (int k = 0; k < fr.numBins; ++k) { fr.specM[k] = mM[k]; fr.specS[k] = mS[k]; }
    }

    // Audio thread: analyze into the current write slot, then publish it with
    // a release store so the GUI thread's acquire load in tryReadFrame() sees
    // a fully-written AnalysisFrame (no torn reads). Then choose the next
    // write slot, EXCLUDING both (a) the slot just published and (b) the slot
    // the reader has claimed (reading_). With 3 slots and 2 excluded indices
    // there is always exactly one safe slot left — so the writer can never
    // overwrite the slot the GUI thread is mid-copy of.
    //
    // Correctness note (fixes a tearing bug in the original design): the
    // reader publishes its claim (reading_) with a release store BEFORE it
    // starts copying, and this loop reads that claim with an acquire load
    // AFTER republishing `ready_`. So once tryReadFrame() has claimed slot r,
    // every subsequent process() call sees the claim and excludes r until the
    // reader claims a different slot. The earlier "skip lastRead_" scheme was
    // broken because lastRead_ was only set AFTER the copy finished — the
    // writer could circle back and overwrite the slot being copied.
    void process(const float* L, const float* R, int n) {
        analyze(L, R, n);                              // fills slots_[writeSlot_]
        ready_.store(writeSlot_, std::memory_order_release);
        const int rd = reading_.load(std::memory_order_acquire);  // slot reader currently holds
        for (int s = 0; s < 3; ++s)                    // next slot excludes published + reader-held
            if (s != writeSlot_ && s != rd) { writeSlot_ = s; break; }
    }
    // GUI thread: copy the latest published slot. Returns false if nothing
    // new has been published since the last successful read. Claims the slot
    // (reading_) with a release store BEFORE copying so the audio thread
    // excludes it from being overwritten while the copy is in flight.
    bool tryReadFrame(AnalysisFrame& out) {
        const int r = ready_.load(std::memory_order_acquire);
        if (r < 0 || r == lastRead_) return false;
        reading_.store(r, std::memory_order_release);  // CLAIM before copying
        out = slots_[r];                               // writer now excludes r
        lastRead_ = r;                                 // GUI-thread-only bookkeeping
        return true;
    }

    // NOTE (deviation from task-5-brief.md step 3): the brief's suggested
    // `frame()` returns slots_[(write_+2)%3] — the "previously completed"
    // slot. That is only correct once process() has advanced writeSlot_ past
    // the slot analyze() just filled. Tasks 3/4 tests call analyzeScalars()/
    // analyze() DIRECTLY (never process()), so writeSlot_ stays 0 and that
    // formula would read an untouched slot (slots_[2]), breaking every
    // existing test. frame() instead returns the *working* slot
    // slots_[writeSlot_] — exactly the slot analyze() just wrote — which
    // matches pre-Task-5 behaviour whether or not process()/publish has run.
    const AnalysisFrame& frame() const { return slots_[writeSlot_]; }
private:
    static constexpr int kFftSize = 4096;   // kNumBins = 2049 in AnalysisFrame
    double fs_ = 48000.0, coef_ = 0.0;
    double mLR_ = 0, mLL_ = 0, mRR_ = 0;
    seam::meter::LevelFollower lvlL_, lvlR_, lvlM_, lvlS_;
    seam::fft::Welch welchM_, welchS_;
    AnalysisFrame slots_[3];
    int writeSlot_ = 0;              // audio-thread only
    std::atomic<int> ready_{-1};     // last published slot (writer->reader), -1 = none
    std::atomic<int> reading_{-1};   // slot the reader is copying (reader->writer), -1 = none
    int lastRead_ = -1;              // GUI-thread only (no cross-thread access)
};

}} // namespace Seam::strx
