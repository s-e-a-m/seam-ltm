//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · alogos — scales, grid and delay table (SDK-free)
//
// FAUST REFERENCE (seam.silvi.lib):
//   algrid(r, D, id, j) = pow(r, (j + alphase(id))/(4*D));
//   aldel(r, D, d, i, k) = select2(n < 2, n : sff.np, n)
//   with {
//       n = int(floor(ma.SR * d * pow(r, (i + k/4)/D) / sma.rsos + 0.5));
//   };
//
//   alline(r, D, g, N, k, d) = seq(i, N, sjm.apfv(almd(r, D, i, k), aldel(r, D, d, i, k), g));
//   alogos(r, D, g, N, id, j) = par(k, 4, alline(r, D, g, N, k, algrid(r, D, id, j)));
//
// Four faces (k) of up to 128 all-pass sections (i). Section i of face k
// lasts d * r^((i + k/4)/D) / c seconds, rounded to the sample and moved to
// the next prime strictly above. The scale r is phi, sqrt 2, e or pi; the
// density D is 1, 2 or 4 sections per factor r^(1/D). The distance d lives on
// a grid per STONE id, in the van der Corput order of phases, so no two
// STONEs play the same spiral. With phi and D = 1 the table is INPHIREV's.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "alogos_arena.h"
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

namespace alogos {

enum class Scale : int { Phi = 0, Sqrt2, E, Pi };
enum class Gain : int { InvPhi = 0, InvSqrt2, InvE, InvPi, Diffusion };

constexpr int kFaces = 4, kMaxSections = 128, kDefaultSections = 17, kMaxStoneId = 8;
constexpr double kDefaultMetres = 3.33, kMinMetres = 1.0, kMaxMetres = 6.0, kCapSeconds = 90.0;

// The scale r of the spiral.
inline double ratio(Scale s) {
    switch (s) {
        case Scale::Phi:   return (1.0 + std::sqrt(5.0)) / 2.0;
        case Scale::Sqrt2: return std::sqrt(2.0);
        case Scale::E:     return std::exp(1.0);
        case Scale::Pi:    return 3.14159265358979323846;
    }
    return 1.0;
}

// The density menu index 0, 1, 2 as sections per factor: 1, 2, 4.
inline int density(int idx) { return idx <= 0 ? 1 : (idx == 1 ? 2 : 4); }

// The all-pass gain g of the menu.
inline double gainValue(Gain g) {
    switch (g) {
        case Gain::InvPhi:    return 2.0 / (1.0 + std::sqrt(5.0));
        case Gain::InvSqrt2:  return 1.0 / std::sqrt(2.0);
        case Gain::InvE:      return 1.0 / std::exp(1.0);
        case Gain::InvPi:     return 1.0 / 3.14159265358979323846;
        case Gain::Diffusion: return std::sqrt(std::sqrt(2.0) - 1.0);
    }
    return 1.0;
}

// alphase: the van der Corput phase of a STONE id; 0 for id 0 (undeclared)
// and for an id out of range.
inline double gridPhase(int stoneId) {
    static const double kTable[kMaxStoneId + 1] =
        { 0.0, 0.0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875 };
    return (stoneId >= 0 && stoneId <= kMaxStoneId) ? kTable[stoneId] : 0.0;
}

// J: the largest j for which the last STONE (phase 7/8) stays within 6 m.
inline int gridMax(Scale s, int D) {
    const double r = ratio(s);
    int j = 0;
    while (std::pow(r, (j + 1 + 0.875) / (4.0 * D)) <= kMaxMetres) ++j;
    return j;
}

// algrid: r^((j + u)/(4 D)) metres.
inline double gridMetres(Scale s, int D, int id, int j) {
    return std::pow(ratio(s), (j + gridPhase(id)) / (4.0 * D));
}

// The grid point nearest in ratio to a distance: j = clamp(round(4 D log_r(m) - u), 0, J).
inline int snapGrid(Scale s, int D, int id, double metres) {
    const double x = 4.0 * D * std::log(metres) / std::log(ratio(s)) - gridPhase(id);
    return std::clamp((int)std::floor(x + 0.5), 0, gridMax(s, D));
}

// N_max: the largest N >= 1 (at most kMaxSections) whose last section of the
// longest face, d * r^((N - 1 + 3/4)/D) / c, lasts no more than kCapSeconds.
inline int nMax(Scale s, int D, double metres) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const double r = ratio(s);
    int n = 1;
    while (n < kMaxSections &&
           metres * std::pow(r, (n + 3.0 / 4.0) / D) / c <= kCapSeconds) ++n;
    return n;
}

// aldel: n kept when below 2, otherwise the prime strictly above n.
inline uint32_t sectionDelay(Scale s, int D, double metres, int i, int k, double fs,
                             const Seam::PrimeSieve& sieve) {
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const int n = (int)std::floor(fs * metres * std::pow(ratio(s), (i + k / 4.0) / D) / c + 0.5);
    return n < 2 ? (uint32_t)n : sieve.nextPrimeAbove((uint32_t)n);
}

// The sieve must reach the prime above any delay: the 90 s cap, rounded, plus
// room for one prime gap (1024 is above every gap below 2^32).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(fs * kCapSeconds + 0.5) + 1024u;
}


// ══ The engine ═══════════════════════════════════════════════════════════

constexpr double kSettleSeconds = 0.25;   // a setting is heard once stable this long
constexpr double kShortRamp = 0.025;      // s, POWER and the fades of a change and of RESET
// The clearing zeroes the arena at this many bytes per sample of the block,
// so it lasts the same time at any block size and rate (stunedrev).
constexpr std::size_t kClearBytesPerSample = 16384;

// Every section is a Seam::MoorerAllpass attached to its slice of the arena.
using Section = Seam::MoorerAllpass;

enum class Status : int { Unprepared = 0, Ready, Clearing, Allocating, AllocFailed };

class Engine {
public:
    Engine() {
        for (auto& face : shown_) for (auto& d : face) d.store(0);
    }
    ~Engine() { release(); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Outside the audio thread (setActive). Sieve, broker, and the arena of
    // the settings held, allocated here synchronously and zeroed (every page
    // resident before process() runs). false when the arena exceeds byteLimit
    // or the memory is not there: the engine stays silent, says "allocation
    // failed", and a later setting that fits recovers it through the broker.
    bool prepare(double fs, std::size_t byteLimit = SIZE_MAX) {
        release();
        fs_ = fs;
        sampleRate_.store(fs);
        try {
            sieve_.reset(new Seam::PrimeSieve(sieveBound(fs)));
            broker_.reset(new ArenaBroker(byteLimit));
        } catch (const std::exception&) {
            release();
            status_.store(Status::AllocFailed);
            return false;
        }
        target_ = pending_;
        computeTable(target_, next_);
        publish(target_, next_);
        settle_ = 0;
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        clearPos_ = clearEnd_ = 0;
        clears_.store(0);
        servedGen_ = resetGen_.load();       // a click before prepare is not replayed
        double* p = broker_->allocateNow(next_.need);
        if (!p) {
            fade_.setTarget(0.0, kShortRamp, fs_); fade_.snap();
            phase_ = Phase::Alloc;
            status_.store(Status::AllocFailed);
            return false;
        }
        arena_ = p;
        arenaSize_ = next_.need;
        arenaBytes_.store(arenaSize_ * sizeof(double));
        attach(next_);
        fade_.setTarget(1.0, kShortRamp, fs_);
        phase_ = Phase::Run;
        status_.store(Status::Ready);
        return true;
    }

    // Outside the audio thread. The arena goes back to the broker, whose
    // destructor joins the worker and frees everything it holds.
    void release() {
        if (broker_ && arena_) broker_->retire(arena_);
        arena_ = nullptr;
        broker_.reset();
        sieve_.reset();
        arenaSize_ = 0;
        arenaBytes_.store(0);
        status_.store(Status::Unprepared);
    }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { pow_.snap(); fade_.snap(); }

    // Audio thread, at the start of a block (or before prepare, where the
    // value is taken at once). Out-of-range values are clamped; the distance
    // and the section count are kept as asked, and snapped and limited to
    // N_max only when the table is built, so a round trip through a scale
    // with a smaller N_max gives the count back. After prepare a value
    // different from the pending one restarts the 250 ms settle.
    void setSettings(int stoneId, double metres, int sections, int scale, int densityIdx, int gain) {
        if (!(metres == metres)) metres = kDefaultMetres;      // NaN
        const Settings s { std::clamp(stoneId, 0, kMaxStoneId),
                           std::clamp(metres, kMinMetres, kMaxMetres),
                           std::clamp(sections, 1, kMaxSections),
                           std::clamp(scale, 0, 3),
                           std::clamp(densityIdx, 0, 2),
                           std::clamp(gain, 0, 4) };
        if (s == pending_) return;
        pending_ = s;
        if (sieve_) { settle_ = (long)std::lround(kSettleSeconds * fs_); return; }
        target_ = s;                                          // before prepare
        Table t; describe(s, t);
        publish(s, t);
    }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    // Any thread (the RESET view): served at the start of the next block.
    void requestReset() { resetGen_.fetch_add(1); }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!sieve_) { zero(out, n); return; }
        events();
        // The faces lose no energy: without this their silent tails would
        // fill the arena with subnormals, and the cost would grow for hours.
        Seam::ScopedNoDenormals noDenormals;
        if (phase_ == Phase::Clear || phase_ == Phase::Alloc) {
            if (phase_ == Phase::Clear) clearChunk(n);
            for (int k = 0; k < n; ++k) pow_.next();
            if (settle_ > 0) settle_ -= n;
            zero(out, n);
            return;
        }
        const int  N = cur_.n;
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
        if (phase_ == Phase::FadeOut && !fade_.active()) decide();   // silent from the next block
    }

    // Readouts (the table being applied and its settings).
    uint32_t    delay(int k, int i) const { return shown_[k][i].load(std::memory_order_relaxed); }
    int         sections() const          { return sections_.load(); }      // effective
    int         sectionsMax() const       { return sectionsMax_.load(); }
    int         stoneId() const           { return stoneId_.load(); }
    double      metres() const            { return metres_.load(); }        // snapped
    int         scale() const             { return scale_.load(); }
    int         densityIdx() const        { return densityIdx_.load(); }
    int         gain() const              { return gain_.load(); }
    std::size_t arenaBytes() const        { return arenaBytes_.load(); }
    double      sampleRate() const        { return sampleRate_.load(); }
    Status      status() const            { return status_.load(); }
    bool        clearing() const          { return status_.load() == Status::Clearing; }
    bool        failed() const            { return status_.load() == Status::AllocFailed; }
    uint32_t    clears() const            { return clears_.load(); }       // sequences begun

    // Test hooks (read when the audio thread is quiet).
    std::size_t sectionOffset(int k, int i) const { return off_[k][i]; }
    std::size_t sectionLength(int k, int i) const { return len_[k][i]; }
    std::size_t arenaDoubles() const              { return arenaSize_; }
    const ArenaBroker* broker() const             { return broker_.get(); }

private:
    // Run: the faces sound. FadeOut: they ring out with no input. Clear: the
    // arena is zeroed in slices, the faces frozen. Alloc: waiting for the
    // broker's arena (or failed); silent, the faces detached.
    enum class Phase { Run, FadeOut, Clear, Alloc };
    struct Settings {
        int id; double metres; int n, scale, dn, gain;
        bool operator==(const Settings& o) const {
            return id == o.id && metres == o.metres && n == o.n && scale == o.scale &&
                   dn == o.dn && gain == o.gain;
        }
    };
    // A delay table: the effective N, the delays of the attached sections
    // (the rest 0), g, and the doubles they need, each delay + 1.
    struct Table {
        uint32_t d[kFaces][kMaxSections];
        int n, nMax;
        double g, metres;
        std::size_t need;
    };

    // Everything but the delays (no sieve needed).
    static void describe(const Settings& s, Table& t) {
        const Scale sc = (Scale)s.scale;
        const int D = density(s.dn);
        t.metres = gridMetres(sc, D, s.id, snapGrid(sc, D, s.id, s.metres));
        t.nMax = nMax(sc, D, t.metres);
        t.n = std::min(s.n, t.nMax);
        t.g = gainValue((Gain)s.gain);
    }

    void computeTable(const Settings& s, Table& t) const {
        describe(s, t);
        const Scale sc = (Scale)s.scale;
        const int D = density(s.dn);
        t.need = 0;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < kMaxSections; ++i) {
                t.d[k][i] = i < t.n ? sectionDelay(sc, D, t.metres, i, k, fs_, *sieve_) : 0u;
                if (i < t.n) t.need += (std::size_t)t.d[k][i] + 1;
            }
    }

    // The identical table: same effective N, same delays for every attached
    // section, same g.
    static bool sameTable(const Table& a, const Table& b) {
        if (a.n != b.n || a.g != b.g) return false;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < a.n; ++i)
                if (a.d[k][i] != b.d[k][i]) return false;
        return true;
    }

    // The start of a block: a RESET click, a settled setting, the arena.
    void events() {
        // computeTable runs here, on the audio thread, once per settled change:
        // up to 512 pow calls, sieve scans and the gridMax/nMax loops, tens of
        // microseconds, no lock or allocation, while the output is faded or silent.
        const uint32_t gen = resetGen_.load();
        if (gen != servedGen_) {
            servedGen_ = gen;
            // Waiting for an arena: the new one comes zeroed, nothing to empty.
            if (phase_ != Phase::Alloc) beginFade();
        }
        if (!(pending_ == target_) && settle_ <= 0) {
            if (phase_ == Phase::Run) {
                computeTable(pending_, next_);
                target_ = pending_;
                if (sameTable(next_, cur_)) publish(target_, next_);   // e.g. "?" -> 1, both phase 0:
                else                        beginFade();               // only the readouts move
            } else if (phase_ == Phase::Alloc) {  // already silent: a newer setting decides now
                computeTable(pending_, next_);
                target_ = pending_;
                clears_.fetch_add(1);
                decide();
            }
        }
        if (phase_ == Phase::Alloc) serveArena();
    }

    // The hand-over on the audio thread's side: the arena of the latest
    // request, if the worker has delivered it; the old one goes back to the
    // worker to be freed. A fresh arena comes zeroed and needs no clear.
    void serveArena() {
        std::size_t got = 0;
        if (double* p = broker_->take(got)) {
            if (arena_) broker_->retire(arena_);
            arena_ = p;
            arenaSize_ = got;
            arenaBytes_.store(got * sizeof(double));
            attach(next_);
            fadeIn();
        } else if (broker_->failed()) {
            status_.store(Status::AllocFailed);
        }
    }

    void beginFade() {
        phase_ = Phase::FadeOut;
        fade_.setTarget(0.0, kShortRamp, fs_);
        status_.store(Status::Clearing);
        clears_.fetch_add(1);
    }

    // Silent: the arena is kept and cleared when the table fits it (no more
    // than the arena, no less than half); otherwise the broker allocates one
    // sized for the table.
    void decide() {
        publish(target_, next_);
        const std::size_t need = next_.need;
        if (arena_ && need <= arenaSize_ && 2 * need >= arenaSize_) {
            broker_->request(0);                  // a pending allocation is no longer wanted
            attach(next_);
            phase_ = Phase::Clear;
            clearPos_ = 0;
            clearEnd_ = need;
            status_.store(Status::Clearing);
        } else {
            broker_->request(need);
            phase_ = Phase::Alloc;
            status_.store(Status::Allocating);
        }
    }

    // The sections onto the arena, each exactly its delay + 1, in order.
    void attach(const Table& t) {
        std::size_t off = 0;
        for (int k = 0; k < kFaces; ++k)
            for (int i = 0; i < kMaxSections; ++i) {
                if (i < t.n) {
                    const std::size_t len = (std::size_t)t.d[k][i] + 1;
                    sec_[k][i].attach(arena_ + off, len);
                    sec_[k][i].setDelay(t.d[k][i]);
                    sec_[k][i].setGain(t.g);
                    off_[k][i] = off; len_[k][i] = len;
                    off += len;
                } else {
                    off_[k][i] = len_[k][i] = 0;
                }
            }
        cur_ = t;
    }

    void publish(const Settings& s, const Table& t) {
        stoneId_.store(s.id); scale_.store(s.scale); densityIdx_.store(s.dn); gain_.store(s.gain);
        metres_.store(t.metres); sections_.store(t.n); sectionsMax_.store(t.nMax);
        if (sieve_)
            for (int k = 0; k < kFaces; ++k)
                for (int i = 0; i < kMaxSections; ++i)
                    shown_[k][i].store(t.d[k][i], std::memory_order_relaxed);
    }

    // A slice of the arena per block, proportional to the block: the faces
    // are frozen, so no uncleared history can flow into a cleared buffer.
    // Waiting for the broker instead: the arena arrives at the start of a block.
    void clearChunk(int n) {
        const std::size_t want = (std::size_t)n * kClearBytesPerSample / sizeof(double);
        const std::size_t m = std::min(want, clearEnd_ - clearPos_);
        std::memset(arena_ + clearPos_, 0, m * sizeof(double));
        clearPos_ += m;
        if (clearPos_ < clearEnd_) return;
        for (auto& face : sec_) for (Section& s : face) s.clear();
        fadeIn();
    }

    void fadeIn() {
        phase_ = Phase::Run;
        fade_.setTarget(1.0, kShortRamp, fs_);
        status_.store(Status::Ready);
    }

    template <class T>
    static void zero(T* const* out, int n) {
        for (int k = 0; k < kFaces; ++k) std::fill(out[k], out[k] + n, (T)0);
    }

    double fs_ = 96000.0;
    Settings pending_ { 1, kDefaultMetres, kDefaultSections, 0, 0, 0 };
    Settings target_  { 1, kDefaultMetres, kDefaultSections, 0, 0, 0 };
    Table cur_ {}, next_ {};
    long   settle_ = 0;                       // samples left of the 250 ms
    Section sec_[kFaces][kMaxSections];
    std::size_t off_[kFaces][kMaxSections] {}, len_[kFaces][kMaxSections] {};
    double* arena_ = nullptr;                 // owned; handed back through the broker
    std::size_t arenaSize_ = 0, clearPos_ = 0, clearEnd_ = 0;
    std::unique_ptr<Seam::PrimeSieve> sieve_;
    std::unique_ptr<ArenaBroker> broker_;
    Seam::LinearRamp pow_, fade_;
    Phase phase_ = Phase::Run;
    uint32_t servedGen_ = 0;

    std::atomic<uint32_t>    resetGen_{0}, clears_{0};
    std::atomic<Status>      status_{Status::Unprepared};
    std::atomic<int>         stoneId_{1}, sections_{kDefaultSections}, sectionsMax_{0};
    std::atomic<int>         scale_{0}, densityIdx_{0}, gain_{0};
    std::atomic<double>      metres_{0.0};
    std::atomic<std::size_t> arenaBytes_{0};
    std::atomic<double>      sampleRate_{0.0};
    std::atomic<uint32_t>    shown_[kFaces][kMaxSections];
};

} // namespace alogos
