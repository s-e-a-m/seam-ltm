//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · addelay — SDK-free DSP core (header-only, unit-testable).
//
// The two halves of "distance", split by phase type:
//   1. bulk propagation delay  — integer samples, nextPrime (ddelay core;
//      LINEAR phase, exact, no interpolation)
//   2. atmospheric absorption  — minimum-phase shelf/cascade fitted to
//      ISO 9613-1 alpha*d (seam_airabsorption.h; MIN phase, zero latency)
//   3. optional geometric 1/r spreading — a scalar broadband gain.
//
// One distance -> one delay -> one filter coefficient set -> one spreading
// gain, shared by all four channels. Filter STATE is per channel.
//
// FAUST REFERENCE (seam.math.lib): isos=331.4; imt2samp(mt)=int(mt*SR/isos);
//   next prime from sff.np. FAUST REFERENCE (seam.filters.lib): air filter.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_airabsorption.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace Seam { namespace addelay {

inline bool isPrime(int n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if ((n & 1) == 0) return false;
    const int lim = (int)std::sqrt((double)n);
    for (int i = 3; i <= lim; i += 2) if (n % i == 0) return false;
    return true;
}
inline int nextPrime(int n) {
    if (n < 2) return 2;
    int c = (n & 1) ? n + 2 : n + 1;
    while (!isPrime(c)) c += 2;
    return c;
}

class AirDelay {
public:
    static constexpr int kNumChannels = 4;
    static constexpr double kSpeedOfSound = 331.4;   // m/s (matches ddelay / seam.math.lib::isos)
    static constexpr double kMaxDistance  = 30.0;    // m
    static constexpr int    kBufferSize   = 32768;   // > 30m @ 192k, power of two
    static constexpr int    kBufferMask   = kBufferSize - 1;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        for (int c = 0; c < kNumChannels; ++c) buf_[c].assign(kBufferSize, 0.0);
        writeIndex_ = 0;
        redesign_();
        for (int c = 0; c < kNumChannels; ++c) { filt_[c].configure(design_); filt_[c].reset(); }
    }

    void reset() {
        for (int c = 0; c < kNumChannels; ++c) {
            std::fill(buf_[c].begin(), buf_[c].end(), 0.0);
            filt_[c].reset();
        }
        writeIndex_ = 0;
    }

    void setParams(double dMeters, double tempC, double rhPercent,
                   air::Topology topo, bool spreading) {
        dMeters   = std::clamp(dMeters, 0.0, kMaxDistance);
        d_ = dMeters; t_ = tempC; rh_ = rhPercent; topo_ = topo; spreading_ = spreading;
        updateDelay_();
        redesign_();
        for (int c = 0; c < kNumChannels; ++c) filt_[c].configure(design_); // shared coeffs, keep state
        spreadGain_ = spreading_ ? air::spreadingGain(d_) : 1.0;
    }

    int delaySamples() const { return delay_; }

    void process(const float* const* in, float* const* out, int n)  { run_(in, out, n); }
    void process(const double* const* in, double* const* out, int n){ run_(in, out, n); }

private:
    template <typename S>
    void run_(const S* const* in, S* const* out, int n) {
        int w = writeIndex_;
        const int delay = delay_;
        for (int i = 0; i < n; ++i) {
            const int r = (w - delay) & kBufferMask;
            for (int c = 0; c < kNumChannels; ++c) {
                buf_[c][w] = (double)in[c][i];
                const double delayed = buf_[c][r];
                const double filtered = filt_[c].process(delayed);   // per-channel state
                out[c][i] = (S)(filtered * spreadGain_);              // shared scalar
            }
            w = (w + 1) & kBufferMask;
        }
        writeIndex_ = w;
    }

    void updateDelay_() {
        const double mm = std::round(d_ * 1000.0) / 1000.0;          // mm quantization (ddelay idiom)
        const int nRaw = (int)std::lround(mm * fs_ / kSpeedOfSound);
        int d = (nRaw < 2) ? nRaw : nextPrime(nRaw);
        if (d > kBufferSize - 1) d = kBufferSize - 1;
        if (d < 0) d = 0;
        delay_ = d;
    }

    void redesign_() {
        design_ = air::designAirFilter(d_, t_, rh_, fs_, topo_);
    }

    double fs_ = 48000.0;
    double d_ = 0.0, t_ = 20.0, rh_ = 50.0;
    air::Topology topo_ = air::Topology::Shelf;
    bool   spreading_ = false;
    double spreadGain_ = 1.0;
    int    delay_ = 0, writeIndex_ = 0;

    std::vector<double> buf_[kNumChannels];
    air::AirFilterDesign design_;
    air::AirFilter       filt_[kNumChannels];
};

}} // namespace Seam::addelay
