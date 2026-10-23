#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "m2xhgr_dsp.h"
#include "seam_meter.h"

#include <cstdint>
#include <vector>

namespace {

// Verbatim per-sample body of M2XHGRProcessor::processHaarMono before the
// extraction (commit 2afb441): Haar in double, cast back, then gainRotateYPR
// in the sample type. The extracted header must reproduce it bit for bit.
template <typename T>
void referenceTick (Seam::HaarState<double>& haar, T x,
                    T g1, T g2, T g3, T yaw, T pitch, T roll, T out[4])
{
    double d = static_cast<double>(x);
    double da0, da1, da2, da3;
    Seam::haarDecompose (haar, d, da0, da1, da2, da3);

    T a0 = static_cast<T>(da0);
    T a1 = static_cast<T>(da1);
    T a2 = static_cast<T>(da2);
    T a3 = static_cast<T>(da3);

    Seam::gainRotateYPR (g1, g2, g3, yaw, pitch, roll,
                         a0, a1, a2, a3,
                         out[0], out[1], out[2], out[3]);
}

std::vector<double> lcgNoise (int n)
{
    std::vector<double> v (n);
    uint32_t s = 12345u;
    for (auto& x : v) {
        s = s * 1664525u + 1013904223u;
        x = (double)(s >> 8) / (double)(1u << 24) * 2.0 - 1.0;
    }
    return v;
}

template <typename T>
void checkBitIdentical ()
{
    const T yaw = T(0.7), pitch = T(-0.3), roll = T(1.9);
    const T g1 = T(seam::meter::db2lin (-3.0));
    const T g2 = T(seam::meter::db2lin (2.0));
    const T g3 = T(seam::meter::db2lin (6.0));

    Seam::HaarState<double> refHaar;
    Seam::m2xhgr::Bank bank;
    for (double s : lcgNoise (4096)) {
        T ref[4], out[4];
        referenceTick<T> (refHaar, T(s), g1, g2, g3, yaw, pitch, roll, ref);
        Seam::m2xhgr::tick<T> (bank, T(s), g1, g2, g3, yaw, pitch, roll, out);
        for (int k = 0; k < 4; ++k)
            REQUIRE(out[k] == ref[k]);
    }
}

} // namespace

TEST_CASE("m2xhgr tick: bit-identical to the pre-refactor processor, double") {
    checkBitIdentical<double> ();
}

TEST_CASE("m2xhgr tick: bit-identical to the pre-refactor processor, float") {
    checkBitIdentical<float> ();
}

TEST_CASE("m2xhgr Bank::reset clears the Haar memory") {
    Seam::m2xhgr::Bank bank;
    double a[4];
    bank.decompose (1.0, a);
    bank.reset ();
    bank.decompose (0.0, a);
    for (int k = 0; k < 4; ++k)
        CHECK(a[k] == 0.0);
}
