//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_delays.h — an integer delay line, the C++ side of de.delay
//
// FAUST REFERENCE (delays.lib, de, standard):
//   de.delay(maxdel, d) : y[n] = x[n-d], d an integer, 0 <= d <= maxdel
//
// The standard library has the delay, so SEAM's Faust has no delays.lib of
// its own (2026-09-29); C++ has none, and a plugin must not define one
// (filters and blocks live in _common/). The buffer belongs to the caller,
// sized exactly for the longest d it will ask (len >= dmax + 1), as
// seam_moorer.h takes it. Written 2026-10-02 for SSCDO#2's delRM; DDELAY and
// ADDELAY still carry their own ring buffers.
//
// Per sample: write x, then read d samples back, so d = 0 returns x itself
// as de.delay does, and a new d is heard on the next tick.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstddef>
#include <cstdint>

namespace Seam {

class IntegerDelay {
public:
    // buf holds len doubles, zeroed by the caller; len >= the longest d + 1.
    void attach(double* buf, std::size_t len) { buf_ = buf; len_ = len; clear(); setDelay(d_); }

    // The write index back to the start, the history to zero.
    void clear() {
        for (std::size_t i = 0; i < len_; ++i) buf_[i] = 0.0;
        pos_ = 0;
    }

    // 0 <= d <= len - 1; a longer d is clamped to len - 1, in every build:
    // the clamp is the contract (seam_delays_test), so no assert stops a
    // Debug host on the audio thread for a case the line already handles.
    void setDelay(uint32_t d) {
        d_ = (len_ > 0 && d >= len_) ? (uint32_t)(len_ - 1) : d;
    }
    uint32_t    delay() const  { return d_; }
    std::size_t length() const { return len_; }

    double tick(double x) {
        buf_[pos_] = x;
        std::size_t r = pos_ + len_ - d_;
        if (r >= len_) r -= len_;
        const double y = buf_[r];
        if (++pos_ == len_) pos_ = 0;
        return y;
    }

private:
    double*     buf_ = nullptr;
    std::size_t len_ = 0, pos_ = 0;
    uint32_t    d_ = 0;
};

} // namespace Seam
