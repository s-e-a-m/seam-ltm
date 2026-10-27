//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · inphirev — the grid and the delay table (SDK-free)
//
// FAUST REFERENCE (seam.silvi.lib):
//   ipline(N, k, d) = seq(i, N, sjm.apfv(ipmd(i, k), ipdel(d, i, k), sma.rphi(1)));
//   inphirev(N, id, j) = par(k, 4, ipline(N, k, ipgrid(id, j)));
//   ipphase(id) = ba.take(max(id,1), (0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875));
//   ipgrid(id, j) = pow(sma.phi(1), (j + ipphase(id))/4);
//   ipdel(d, i, k) = select2(n < 2, n : sff.np, n)
//   with {
//       n = int(floor(ma.SR * d * pow(sma.phi(1), i + k/4) / sma.rsos + 0.5));
//   };
//
// Four faces (k) of up to 18 all-pass sections (i). Section i of face k lasts
// d * phi^(i + k/4) / c seconds, rounded to the sample and moved to the next
// prime strictly above. The distance d lives on a grid per STONE id, in the
// van der Corput order of phases, so no two STONEs play the same spiral.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_basics.h"
#include "seam_denormals.h"
#include "seam_moorer.h"
#include "seam_primes.h"
#include "seam_ramp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace inphirev {

constexpr int kFaces = 4, kMaxSections = 18, kDefaultSections = 17;
constexpr int kGridMax = 14, kDefaultGrid = 10, kMaxStoneId = 8;

// sma.phi(1) and g = 1/phi (sma.rphi(1)).
inline const double kPhi = (1.0 + std::sqrt(5.0)) / 2.0;
inline const double kG   = 1.0 / kPhi;

// ipphase: the van der Corput phase of a STONE id; 0 for id 0 (undeclared)
// and for an id out of range.
inline double gridPhase(int stoneId) {
    static const double kTable[kMaxStoneId + 1] =
        { 0.0, 0.0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875 };
    return (stoneId >= 0 && stoneId <= kMaxStoneId) ? kTable[stoneId] : 0.0;
}

// ipgrid: phi^((j + u)/4) metres.
inline double gridMetres(int stoneId, int j) {
    return std::pow(kPhi, (j + gridPhase(stoneId)) / 4.0);
}

// ipdel: n kept when below 2, otherwise the prime strictly above n.
inline uint32_t sectionDelay(double metres, int i, int k, double fs, const Seam::PrimeSieve& s) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const int n = (int)std::floor(fs * metres * std::pow(kPhi, i + k / 4.0) / c + 0.5);
    return n < 2 ? (uint32_t)n : s.nextPrimeAbove((uint32_t)n);
}

// The largest n of the plugin (id 8, j = 14, section 17, face 3) plus room
// for one prime gap: 1024 is above every gap below 2^32 (the largest is 336).
inline uint32_t sieveBound(double fs) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const double d = gridMetres(kMaxStoneId, kGridMax);
    return (uint32_t)std::floor(fs * d * std::pow(kPhi, (kMaxSections - 1) + 3 / 4.0) / c + 0.5) + 1024u;
}

constexpr double kSettleSeconds = 0.25;   // a setting is heard once stable this long
constexpr double kShortRamp = 0.025;      // s, POWER and the fades of RESET
// RESET zeroes the arena at this many bytes per sample of the block, so the
// clearing lasts the same time at any block size and rate (stunedrev).
constexpr std::size_t kClearBytesPerSample = 16384;

// Every section is a Seam::MoorerAllpass, g = 1/phi, attached to its slice
// of the arena.
using Section = Seam::MoorerAllpass;

// The buffer of section i of face k: the longest delay it can be asked, over
// every id (0...8) and grid point, plus one (w[n-(t-1)] with w[n]).
inline std::size_t sectionLength(int k, int i, double fs, const Seam::PrimeSieve& s) {
    uint32_t longest = 0;
    for (int id = 0; id <= kMaxStoneId; ++id)
        for (int j = 0; j <= kGridMax; ++j)
            longest = std::max(longest, sectionDelay(gridMetres(id, j), i, k, fs, s));
    return (std::size_t)longest + 1;
}

inline std::size_t arenaDoubles(double fs, const Seam::PrimeSieve& s) {
    std::size_t total = 0;
    for (int k = 0; k < kFaces; ++k)
        for (int i = 0; i < kMaxSections; ++i) total += sectionLength(k, i, fs, s);
    return total;
}

enum class Status : int { Unprepared = 0, Ready, Clearing, AllocFailed };

class Engine {
public:
    // Outside the audio thread (setActive). Sieve, arena (zeroed now, so every
    // page is resident before process() runs), the table of the settings
    // held. false when the arena exceeds byteLimit or the memory is not
    // there: the engine stays silent.
    bool prepare(double fs, std::size_t byteLimit = SIZE_MAX) {
        release();
        fs_ = fs;
        sampleRate_.store(fs);
        try {
            sieve_.reset(new Seam::PrimeSieve(sieveBound(fs)));
            const std::size_t total = arenaDoubles(fs, *sieve_);
            if (total > byteLimit / sizeof(double)) {
                release();
                status_.store(Status::AllocFailed);
                return false;
            }
            arena_.reset(new double[total]());
            arenaSize_ = total;
        } catch (const std::bad_alloc&) {
            release();
            status_.store(Status::AllocFailed);
            return false;
        }
        std::size_t off = 0;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < kMaxSections; ++i) {
                const std::size_t len = sectionLength(k, i, fs, *sieve_);
                sec_[k][i].attach(arena_.get() + off, len);
                sec_[k][i].setGain(kG);
                off += len;
            }
        arenaBytes_.store(arenaSize_ * sizeof(double));
        applyTable(pending_);
        snap_ = pending_;
        settle_ = 0;
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        fade_.setTarget(1.0, kShortRamp, fs_);
        phase_ = Phase::Run;
        clearPos_ = 0;
        clears_.store(0);
        servedGen_ = resetGen_.load();       // a click before prepare is not replayed
        status_.store(Status::Ready);
        return true;
    }

    void release() {
        arena_.reset();
        sieve_.reset();
        arenaSize_ = 0;
        arenaBytes_.store(0);
        status_.store(Status::Unprepared);
    }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { pow_.snap(); fade_.snap(); }

    // Audio thread, at the start of a block (or before prepare, where the
    // value is taken at once). Out-of-range values are clamped. After
    // prepare a value different from the pending one restarts the 250 ms
    // settle; once the pause has passed the table is rebuilt and RESET runs.
    void setSettings(int stoneId, int j, int sections) {
        const Settings s { std::min(kMaxStoneId, std::max(0, stoneId)),
                           std::min(kGridMax, std::max(0, j)),
                           std::min(kMaxSections, std::max(1, sections)) };
        if (s == pending_) return;
        pending_ = s;
        if (arena_) settle_ = (long)std::lround(kSettleSeconds * fs_);
        else        { applied_ = s; snap_ = s; publish(s); }
    }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    // Any thread (the RESET view): served at the start of the next block.
    void requestReset() { resetGen_.fetch_add(1); }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!arena_) { zero(out, n); return; }
        // The faces lose no energy: without this their silent tails would
        // fill the arena with subnormals, and the cost would grow for hours.
        Seam::ScopedNoDenormals noDenormals;
        const uint32_t gen = resetGen_.load();
        if (gen != servedGen_) {                  // a click: fade, then clear
            servedGen_ = gen;
            beginClear();
        } else if (phase_ == Phase::Run && !(pending_ == applied_) && settle_ <= 0) {
            snap_ = pending_;                     // stable for 250 ms: apply it
            beginClear();
        }
        if (phase_ == Phase::Clear) {
            clearChunk(n);
            for (int k = 0; k < n; ++k) { pow_.next(); }
            if (settle_ > 0) settle_ -= n;
            zero(out, n);
            return;
        }
        const int  N = applied_.n;
        const bool feeding = phase_ == Phase::Run;
        for (int s = 0; s < n; ++s) {
            const double gout = pow_.next() * fade_.next();
            for (int k = 0; k < kFaces; ++k) {
                double y = feeding ? (double)in[k][s] : 0.0;
                for (int i = 0; i < N; ++i) y = sec_[k][i].tick(y);
                out[k][s] = (T)(y * gout);
            }
        }
        if (settle_ > 0) settle_ -= n;
        if (phase_ == Phase::FadeOut && !fade_.active()) {
            phase_ = Phase::Clear;                // the faces freeze from the next block
            clearPos_ = 0;
            applyTable(snap_);                    // the memory is about to be emptied
        }
    }

    // Readouts (the applied settings and table).
    uint32_t    delay(int k, int i) const { return sec_[k][i].delay(); }
    int         sections() const          { return sections_.load(); }
    int         stoneId() const           { return stoneId_.load(); }
    int         grid() const              { return grid_.load(); }
    double      metres() const            { return metres_.load(); }
    std::size_t arenaBytes() const        { return arenaBytes_.load(); }
    double      sampleRate() const        { return sampleRate_.load(); }
    bool        clearing() const          { return status_.load() == Status::Clearing; }
    bool        failed() const            { return status_.load() == Status::AllocFailed; }
    Status      status() const            { return status_.load(); }
    uint32_t    clears() const            { return clears_.load(); }   // sequences begun

private:
    enum class Phase { Run, FadeOut, Clear };
    struct Settings {
        int id, j, n;
        bool operator==(const Settings& o) const { return id == o.id && j == o.j && n == o.n; }
    };

    void beginClear() {
        phase_ = Phase::FadeOut;
        fade_.setTarget(0.0, kShortRamp, fs_);
        status_.store(Status::Clearing);
        clears_.fetch_add(1);
    }

    void publish(const Settings& s) {
        stoneId_.store(s.id); grid_.store(s.j); sections_.store(s.n);
        metres_.store(gridMetres(s.id, s.j));
    }

    // The whole table of 18 x 4 delays, not only the first N sections.
    void applyTable(const Settings& s) {
        const double d = gridMetres(s.id, s.j);
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < kMaxSections; ++i)
                sec_[k][i].setDelay(sectionDelay(d, i, k, fs_, *sieve_));
        applied_ = s;
        publish(s);
    }

    // A slice of the arena per block, proportional to the block: the faces
    // are frozen, so no uncleared history can flow into a cleared buffer.
    void clearChunk(int n) {
        const std::size_t want = (std::size_t)n * kClearBytesPerSample / sizeof(double);
        const std::size_t m = std::min(want, arenaSize_ - clearPos_);
        std::memset(arena_.get() + clearPos_, 0, m * sizeof(double));
        clearPos_ += m;
        if (clearPos_ < arenaSize_) return;
        for (auto& face : sec_) for (Section& s : face) s.clear();
        phase_ = Phase::Run;
        fade_.setTarget(1.0, kShortRamp, fs_);
        status_.store(Status::Ready);
    }

    template <class T>
    static void zero(T* const* out, int n) {
        for (int k = 0; k < kFaces; ++k) std::fill(out[k], out[k] + n, (T)0);
    }

    double fs_ = 96000.0;
    Settings pending_ { 1, kDefaultGrid, kDefaultSections };
    Settings applied_ { 1, kDefaultGrid, kDefaultSections };
    Settings snap_    { 1, kDefaultGrid, kDefaultSections };
    long   settle_ = 0;                       // samples left of the 250 ms
    Section sec_[kFaces][kMaxSections];
    std::unique_ptr<double[]> arena_;
    std::size_t arenaSize_ = 0, clearPos_ = 0;
    std::unique_ptr<Seam::PrimeSieve> sieve_;
    Seam::LinearRamp pow_, fade_;
    Phase phase_ = Phase::Run;
    uint32_t servedGen_ = 0;

    std::atomic<uint32_t>    resetGen_{0}, clears_{0};
    std::atomic<Status>      status_{Status::Unprepared};
    std::atomic<int>         stoneId_{1}, grid_{kDefaultGrid}, sections_{kDefaultSections};
    std::atomic<double>      metres_{0.0};
    std::atomic<std::size_t> arenaBytes_{0};
    std::atomic<double>      sampleRate_{0.0};
};

} // namespace inphirev
