#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_butterworth.h"
#include "ref/lmo_ref.h"
#include <cmath>
#include <algorithm>

using namespace Seam;

template <int N>
static double maxErr(ButterworthType t, double fc, const double* ref, int n) {
    ButterworthSVF<N> f;
    f.setType(t);
    f.setFrequency(fc, 96000.0);
    double e = 0.0, pk = 0.0;
    for (int k = 0; k < n; ++k) {
        const double y = f.tick(k == 0 ? 1.0 : 0.0);
        e  = std::max(e, std::fabs(y - ref[k]));
        pk = std::max(pk, std::fabs(ref[k]));
    }
    return e / pk;
}

TEST_CASE("order 24 equals fi.highpass/fi.lowpass(24, 97.44) at 96 kHz") {
    CHECK(maxErr<24>(ButterworthType::Highpass, 97.44, lmoref::kBw[0], 2048) < 1e-12);
    CHECK(maxErr<24>(ButterworthType::Lowpass,  97.44, lmoref::kBw[1], 2048) < 1e-12);
}

TEST_CASE("order 2 equals fi.highpass/fi.lowpass(2, 1000) at 96 kHz") {
    CHECK(maxErr<2>(ButterworthType::Highpass, 1000.0, lmoref::kBw[2], 2048) < 1e-12);
    CHECK(maxErr<2>(ButterworthType::Lowpass,  1000.0, lmoref::kBw[3], 2048) < 1e-12);
}

TEST_CASE("-3.01 dB at fc, designed per rate") {
    for (double fs : {48000.0, 96000.0, 192000.0}) {
        ButterworthSVF<24> f;
        f.setType(ButterworthType::Lowpass);
        f.setFrequency(1000.0, fs);
        // steady-state amplitude of a 1 kHz sine after 1 s
        const int n = (int)fs, tail = (int)(fs / 10);
        double pk = 0.0;
        for (int k = 0; k < n; ++k) {
            const double y = f.tick(std::sin(2.0 * M_PI * 1000.0 * k / fs));
            if (k >= n - tail) pk = std::max(pk, std::fabs(y));
        }
        CHECK(std::fabs(20.0 * std::log10(pk) + 3.0103) < 0.003);
    }
}

TEST_CASE("reset clears the state") {
    ButterworthSVF<24> f;
    f.setType(ButterworthType::Highpass);
    f.setFrequency(97.44, 96000.0);
    const double y0 = f.tick(1.0);
    for (int k = 0; k < 1000; ++k) f.tick(0.3);
    f.reset();
    CHECK(f.tick(1.0) == y0);
}
