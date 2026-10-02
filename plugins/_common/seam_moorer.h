//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_moorer.h — Moorer's all-pass, the C++ side of seam.moorer.lib
//
// FAUST REFERENCE (seam.moorer.lib, sjm):
//   apfv(md,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(md,t-1),_)~(0-_) : mem+_;
//
// J. A. Moorer, "About This Reverberation Business" (1979), fig. 2(b): one
// multiplier, so the section is all-pass by structure for any g and any
// rounding. apfv is the form with the buffer as a parameter (written
// 2026-09-29 for Davide Tedesco's stunedrev); here the buffer belongs to the
// caller, so many sections can share one arena or each own a vector.
//
// Unrolled per sample, v being the delay's output held one sample by the
// loop (and by the output's mem):
//   a = -g·(x - v)      the only multiplier
//   w = a + x           written to the buffer
//   y = v + a
//   v = w[n-(t-1)]      read after the write, with the t of this sample
// so a new t is heard one sample after it is set, as in the Faust.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstddef>
#include <cstdint>

namespace Seam {

class MoorerAllpass {
public:
    // buf holds len doubles, zeroed by the caller; len >= the longest delay + 1.
    void attach(double* buf, std::size_t len) { buf_ = buf; len_ = len; clear(); }

    // The state to zero. The buffer is the caller's to zero (stunedrev's
    // RESET zeroes its arena in slices, over several blocks).
    void clear() { pos_ = 0; v_ = 0.0; }

    void     setDelay(uint32_t t) { t_ = t; }   // 1 <= t <= len - 1
    uint32_t delay() const        { return t_; }
    void     setGain(double g)    { g_ = g; }
    double   gain() const         { return g_; }

    double tick(double x) {
        const double a = -g_ * (x - v_);
        buf_[pos_] = a + x;
        const double y = v_ + a;
        std::size_t r = pos_ + len_ - (t_ - 1);
        if (r >= len_) r -= len_;
        v_ = buf_[r];
        if (++pos_ == len_) pos_ = 0;
        return y;
    }

private:
    double*     buf_ = nullptr;
    std::size_t len_ = 0, pos_ = 0;
    uint32_t    t_ = 1;
    double      g_ = 0.0;
    double      v_ = 0.0;
};

} // namespace Seam
