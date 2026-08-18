// SEAM-LTM · strx_dsp — SDK-free analysis core for the STONE observation analyzer.
//
// FAUST REFERENCE:
//   seam.stereophony.lib : sst.sdmx   — Blumlein M/S sum-and-difference matrix
//   seam.analyzers.lib   : san.correlation, san.width, san.panorama, san.vectorangle
//   seam.analyzers.lib   : san.thirdoctave_levels_ab, san.pinkcomp — the ISO
//                          266 band table, ported by hand in strx_bands.h
// The spectral CURVE uses a Welch FFT (seam_fft.h) for a finer display, where
// an.mth_octave_spectral_level is the idiomatic SR-independent equivalent. The
// band TABLE is the filterbank itself, and is a real port rather than a
// citation — see strx_bands.h.
#pragma once
#include "seam_fft.h"
#include "strx_bands.h"
#include "seam_meter.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

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
    // Per-bin max since the last resetHold(). Meaningful when a swept emitter
    // is sounding: each bin is excited once as the sweep passes, so holding the
    // maximum draws the response curve over one pass. Always published; the
    // view decides whether to draw it.
    float holdM[kNumBins] = {};  // dB
    float holdS[kNumBins] = {};  // dB
    // Cross-loop accumulation: per-bin MIN over completed passes' hold
    // curves (dB). Meaningful only when accPasses > 0. The MIN keeps what
    // EVERY pass contains — the system response — and discards intermittent
    // disturbances, which are absent from at least one pass.
    float accM[kNumBins] = {};  // dB
    float accS[kNumBins] = {};  // dB
    int accPasses = 0;          // completed passes folded so far
    int numBins = 0;

    // ISO 266 third-octave read of a pink measurement: dB to dial into the
    // amplifier per band, and how far each band has come towards a trustworthy
    // reading (0..1). Meaningful only while a pink emitter is sounding — the
    // view decides, the analyser always publishes.
    float bandComp[kNumBands]   = {};
    float bandSettle[kNumBands] = {};
    // Leading bands this sample rate can measure; the rest are not reported.
    int   bandCount = 0;
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
        const double tau = glide_ ? kGlideTauSec : kPinkTauSec;
        welchM_.prepare(kFftSize, tau, fs_);
        welchS_.prepare(kFftSize, tau, fs_);
        bands_.prepare(fs_);
        reset();
    }
    void reset() {
        lvlL_.reset(); lvlR_.reset(); lvlM_.reset(); lvlS_.reset();
        mLR_ = mLL_ = mRR_ = 0.0;
        welchM_.reset(); welchS_.reset();
        bands_.reset();
        std::fill(accM_, accM_ + AnalysisFrame::kNumBins, -120.0f);
        std::fill(accS_, accS_ + AnalysisFrame::kNumBins, -120.0f);
        accPasses_ = 0;
        foldArmed_ = false;
        for (auto& s : slots_) s = AnalysisFrame{};
        // SPSC triple-buffer to distinct init indices (runs single-threaded here).
        back_.store(0, std::memory_order_relaxed);  // index 0, not dirty
        writeBuf_ = 1;
        front_    = 2;
    }
    // Switch the live average between the pink and glide time constants.
    // Idempotent: repeated calls with the same value do nothing, so the
    // processor can call this every block without disturbing the analysis.
    void setGlideMode(bool on) {
        if (on == glide_) return;
        glide_ = on;
        const double tau = on ? kGlideTauSec : kPinkTauSec;
        welchM_.setEmaTau(tau);
        welchS_.setEmaTau(tau);
        // The band table reads a stationary pink field; a sweep passing
        // through it would leave a smear that outlives the sweep, since the
        // low bands average over ten seconds. Every change of source starts a
        // fresh measurement.
        bands_.reset();
    }
    bool glideMode() const { return glide_; }

    // Clear the max-hold. Driven by the calibration bus: a new pass starts a
    // new measurement, so the previous pass's curve must not linger.
    void resetHold() { welchM_.resetHold(); welchS_.resetHold(); }

    // A completed pass boundary. The accumulator only folds a pass it
    // observed from its very beginning — a SessionStart can land mid-pass
    // (editor opened, or processing reactivated, while ltglide is already
    // running a loop), so the hold at THIS boundary may only cover the tail
    // of that pass. Folding a partial hold under MIN would floor-pin every
    // bin the pass never got to re-excite, for the rest of the session. So:
    // the first completePass() after arming (a fresh session or reset) is a
    // DISCARD — it clears the hold and arms folding, contributing nothing to
    // acc — and only the boundaries after that fold. This costs one good
    // pass in the clean workflow (glide starts, then the first full sweep is
    // thrown away), which is irrelevant next to a multi-minute LOOP run.
    // Once armed, the fold COPIES on first use (a MIN against the -120 init
    // floor would pin the accumulation there forever). dB min == linear min
    // (monotone map), so fold in dB.
    void completePass() {
        if (!foldArmed_) {
            // This boundary's hold may be a partial pass (mid-pass session
            // join) — discard it and arm folding for the next boundary.
            resetHold();
            foldArmed_ = true;
            return;
        }
        const float* hM = welchM_.holdDb();
        const float* hS = welchS_.holdDb();
        const int n = welchM_.numBins();
        for (int k = 0; k < n; ++k) {
            accM_[k] = (accPasses_ == 0) ? hM[k] : std::min(accM_[k], hM[k]);
            accS_[k] = (accPasses_ == 0) ? hS[k] : std::min(accS_[k], hS[k]);
        }
        ++accPasses_;
        resetHold();
    }
    // A new measurement session: previous accumulation and hold both go, and
    // folding is disarmed again — the very next completePass() boundary is
    // itself an unknown-origin pass (session start can land mid-pass), so it
    // must discard rather than fold; see completePass() above.
    void startSession() {
        std::fill(accM_, accM_ + AnalysisFrame::kNumBins, -120.0f);
        std::fill(accS_, accS_ + AnalysisFrame::kNumBins, -120.0f);
        accPasses_ = 0;
        foldArmed_ = false;
        resetHold();
    }
    int accPasses() const { return accPasses_; }

    void analyzeScalars(const float* L, const float* R, int n) {
        AnalysisFrame& fr = slots_[writeBuf_];
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
        AnalysisFrame& fr = slots_[writeBuf_];   // same slot analyzeScalars() just filled
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
        const float* hM = welchM_.holdDb();
        const float* hS = welchS_.holdDb();
        for (int k = 0; k < fr.numBins; ++k) {
            fr.specM[k] = mM[k]; fr.specS[k] = mS[k];
            fr.holdM[k] = hM[k]; fr.holdS[k] = hS[k];
            fr.accM[k]  = accM_[k]; fr.accS[k] = accS_[k];
        }
        fr.accPasses = accPasses_;
        // ISO 266 band table (san.thirdoctave_levels_ab : san.pinkcomp).
        bands_.process(L, R, n);
        bands_.compensation(fr.bandComp, fr.bandSettle);
        fr.bandCount = bands_.count();
    }

    // Audio thread (producer): fill the working buffer, then publish it by
    // atomically swapping it into `back_` (setting the dirty bit) and reclaiming
    // whatever buffer was there as the next working buffer. The exchange is a
    // SINGLE atomic op — ownership of a whole AnalysisFrame transfers with no
    // load-then-store gap, so the writer and reader can never touch the same
    // buffer regardless of thread interleaving.
    void process(const float* L, const float* R, int n) {
        analyze(L, R, n);                                  // fills slots_[writeBuf_]
        const uint8_t old = back_.exchange(uint8_t(writeBuf_ | kDirty), std::memory_order_acq_rel);
        writeBuf_ = old & kIdxMask;                        // reclaim the old back as our next write buffer
    }
    // GUI thread (consumer): if a new frame has been published, swap our
    // front buffer into `back_` (clearing the dirty bit) and take the freshly
    // published buffer as our new front, then copy it out. Returns false when
    // nothing new has been published since the last read. The exchange again
    // transfers buffer ownership atomically — no gap for the writer to race in.
    bool tryReadFrame(AnalysisFrame& out) {
        if ((back_.load(std::memory_order_acquire) & kDirty) == 0) return false;  // nothing new
        const uint8_t old = back_.exchange(front_, std::memory_order_acq_rel);    // front_ carries no dirty bit
        front_ = old & kIdxMask;                           // take the freshly published buffer
        out = slots_[front_];
        return true;
    }

    // NOTE (deviation from task-5-brief.md step 3): the brief's suggested
    // `frame()` returns the "previously completed" slot, which is only correct
    // after a process() publish has advanced the working index. Tasks 3/4 tests
    // call analyzeScalars()/analyze() DIRECTLY (never process()), so writeBuf_
    // stays at its init value (1). frame() therefore returns the *working*
    // buffer slots_[writeBuf_] — exactly the buffer analyze() just wrote — so
    // those tests read the frame they just filled whether or not process() ran.
    const AnalysisFrame& frame() const { return slots_[writeBuf_]; }
private:
    static constexpr int kFftSize = 4096;   // kNumBins = 2049 in AnalysisFrame
    static constexpr double kPinkTauSec  = 2.0;   // slow average: reads pink noise smoothly
    static constexpr double kGlideTauSec = 0.1;   // fast average: tracks a sweep's moving tone
    double fs_ = 48000.0, coef_ = 0.0;
    double mLR_ = 0, mLL_ = 0, mRR_ = 0;
    bool glide_ = false;
    seam::meter::LevelFollower lvlL_, lvlR_, lvlM_, lvlS_;
    seam::fft::Welch welchM_, welchS_;
    // The ISO 266 filterbank: 62 six-pole sections in double, band-outer per
    // block. Sits beside the Welch analysis rather than replacing it — the
    // curve is for looking, the bands are for reading numbers off.
    BandLevels bands_;
    float accM_[AnalysisFrame::kNumBins] = {};
    float accS_[AnalysisFrame::kNumBins] = {};
    int   accPasses_ = 0;
    // false = the next completePass() boundary discards (observed-from-the-
    // beginning rule: unknown whether the in-progress hold started at a real
    // pass boundary or mid-pass at a session join). Set false by reset() and
    // startSession(); set true by the discarding completePass() itself.
    bool  foldArmed_ = false;

    // --- SPSC triple-buffer: producer = audio thread (process), consumer =
    // GUI thread (tryReadFrame). One atomic packs the "back" buffer index
    // (bits 0-1) + a dirty flag (bit 2). Writer and reader each privately own
    // one buffer index; ownership transfers atomically, so the two threads
    // never touch the same buffer regardless of interleaving. The three indices
    // {front_, writeBuf_, back_&mask} are always a permutation of {0,1,2}.
    static constexpr uint8_t kIdxMask = 0x03;
    static constexpr uint8_t kDirty   = 0x04;
    AnalysisFrame slots_[3];
    std::atomic<uint8_t> back_{0};   // back buffer index + dirty bit (init: index 0, not dirty)
    uint8_t writeBuf_ = 1;           // writer-owned buffer index (audio thread only)
    uint8_t front_    = 2;           // reader-owned buffer index (GUI thread only)
};

}} // namespace Seam::strx
