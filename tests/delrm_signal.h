//──────────────────────────────────────────────────────────────────────────
// delrm_signal.h — the input of the delRM references and tests
//
// A contrabass-clarinet-like low C on each channel: the odd partials 1-9 of
// 29.7 Hz at 1/h, the partial phases shifted per channel, plus LCG noise at
// 0.01 (one generator per channel, seed c+1) and the real chain's DC,
// 3.22e-6. The level steps every 0.5 s through 0.01, 0.05, 0.2, 0.5: below
// the compressor's threshold, across it, and deep into it (the triple
// product is cubic). Included by doc/study/sscdo2/delrm-plugin/refdump.cpp
// and by the tests, so that the Faust render and the C++ render read the
// same samples.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstdint>

struct DelrmSignal {
    explicit DelrmSignal(double fs) : fs_(fs) {
        for (int c = 0; c < 4; ++c) s_[c] = 1u + (uint32_t)c;
    }
    void fill(double* const* in, int n, int channels = 4) {
        static const double kLevel[4] = { 0.01, 0.05, 0.2, 0.5 };
        const double w = 2.0 * 3.141592653589793 * 29.7;
        for (int i = 0; i < n; ++i, ++pos_) {
            const double t = (double)pos_ / fs_;
            int step = (int)(t / 0.5);
            if (step > 3) step = 3;
            for (int c = 0; c < channels; ++c) {
                double x = 0.0;
                for (int h = 1; h <= 9; h += 2) x += std::sin(w * h * t + 0.3 * c * h) / h;
                s_[c] = s_[c] * 1664525u + 1013904223u;
                const double noise = (double)(s_[c] >> 8) / 8388608.0 - 1.0;
                in[c][i] = kLevel[step] * x + 0.01 * noise + 3.22e-6;
            }
        }
    }
private:
    double fs_;
    uint32_t s_[4];
    long pos_ = 0;
};
