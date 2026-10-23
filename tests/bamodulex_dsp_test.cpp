#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "bamodulex_dsp.h"

#include <cstdint>

using Seam::bamodulex::decode;

namespace {

// Verbatim matrix of BAMODULEXProcessor::processMatrix before the
// extraction (commit a35426b).
template <typename T>
void referenceDecode (T w, T y, T z, T x, T out[4])
{
    const T half = static_cast<T>(0.5);
    out[0] = (w + y + z + x) * half;
    out[1] = (w - y - z + x) * half;
    out[2] = (w - y + z - x) * half;
    out[3] = (w + y - z - x) * half;
}

struct Lcg {
    uint32_t s = 987654u;
    double next () {
        s = s * 1664525u + 1013904223u;
        return (double)(s >> 8) / (double)(1u << 24) * 2.0 - 1.0;
    }
};

template <typename T>
void checkBitIdentical ()
{
    Lcg r;
    for (int n = 0; n < 4096; ++n) {
        const T w = T(r.next ()), y = T(r.next ()), z = T(r.next ()), x = T(r.next ());
        T ref[4], out[4];
        referenceDecode (w, y, z, x, ref);
        decode (w, y, z, x, out);
        for (int k = 0; k < 4; ++k)
            REQUIRE(out[k] == ref[k]);
    }
}

void checkRow (double w, double y, double z, double x, const double expected[4])
{
    double out[4];
    decode (w, y, z, x, out);
    for (int k = 0; k < 4; ++k)
        CHECK(out[k] == doctest::Approx (expected[k]));
}

} // namespace

TEST_CASE("bamodulex decode: matrix rows (LFU, RFD, RBU, LBD)") {
    const double fromW[4] = { 0.5,  0.5,  0.5,  0.5 };
    const double fromY[4] = { 0.5, -0.5, -0.5,  0.5 };
    const double fromZ[4] = { 0.5, -0.5,  0.5, -0.5 };
    const double fromX[4] = { 0.5,  0.5, -0.5, -0.5 };
    checkRow (1, 0, 0, 0, fromW);
    checkRow (0, 1, 0, 0, fromY);
    checkRow (0, 0, 1, 0, fromZ);
    checkRow (0, 0, 0, 1, fromX);
}

TEST_CASE("bamodulex decode: bit-identical to the pre-refactor processor") {
    checkBitIdentical<double> ();
    checkBitIdentical<float> ();
}

TEST_CASE("bamodulex decode: orthogonal, energy preserved") {
    Lcg r;
    for (int n = 0; n < 256; ++n) {
        const double w = r.next (), y = r.next (), z = r.next (), x = r.next ();
        double out[4];
        decode (w, y, z, x, out);
        const double eIn  = w * w + y * y + z * z + x * x;
        const double eOut = out[0] * out[0] + out[1] * out[1]
                          + out[2] * out[2] + out[3] * out[3];
        CHECK(eOut == doctest::Approx (eIn));
    }
}
