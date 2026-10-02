//──────────────────────────────────────────────────────────────────────────
// stunedrev_burst.h — the input of the stunedrev references and tests
//
// 50 ms of white noise on each of the four inputs, one LCG per channel
// (seed c+1, Numerical Recipes constants), uniform on [-1, 1), then silence.
// Included by doc/study/sscdo2/stunedrev-plugin/refdump.cpp and by the
// tests, so that the Faust render and the C++ render read the same samples.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstdint>

struct Burst {
    explicit Burst(double fs) : len_(std::lround(0.05 * fs)) {
        for (int c = 0; c < 4; ++c) s_[c] = 1u + (uint32_t)c;
    }
    void fill(double* const* in, int n) {
        for (int i = 0; i < n; ++i, ++pos_)
            for (int c = 0; c < 4; ++c) {
                if (pos_ < len_) {
                    s_[c] = s_[c] * 1664525u + 1013904223u;
                    in[c][i] = (double)(s_[c] >> 8) / 8388608.0 - 1.0;
                } else {
                    in[c][i] = 0.0;
                }
            }
    }
private:
    uint32_t s_[4];
    long len_, pos_ = 0;
};
