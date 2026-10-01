#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_moorer.h"
#include "ref/seam_moorer_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using Seam::MoorerAllpass;

static double impulseErr(double g, const double* ref) {
    std::vector<double> buf(1025, 0.0);
    MoorerAllpass a; a.attach(buf.data(), buf.size()); a.setDelay(37); a.setGain(g);
    double err = 0.0, pk = 0.0;
    for (int k = 0; k < 512; ++k) {
        const double y = a.tick(k == 0 ? 1.0 : 0.0);
        err = std::max(err, std::fabs(y - ref[k]));
        pk  = std::max(pk, std::fabs(ref[k]));
    }
    return err / pk;
}

TEST_CASE("impulse response equals sjm.apfv(1024, 37, 1/sqrt(2))") {
    CHECK(impulseErr(1.0 / std::sqrt(2.0), moorerref::kApfv) < 1e-15);
}

TEST_CASE("impulse response equals sjm.apfv(1024, 37, 0.7): g is a parameter") {
    CHECK(impulseErr(0.7, moorerref::kApfv07) < 1e-15);
}

TEST_CASE("all-pass by structure: the impulse response carries unit energy") {
    for (double g : {0.3, 0.7, 1.0 / std::sqrt(2.0), 0.95}) {
        std::vector<double> buf(64, 0.0);
        MoorerAllpass a; a.attach(buf.data(), buf.size()); a.setDelay(13); a.setGain(g);
        double e = 0.0;
        for (int k = 0; k < 200000; ++k) { const double y = a.tick(k == 0 ? 1.0 : 0.0); e += y * y; }
        CAPTURE(g);
        CHECK(std::fabs(e - 1.0) < 1e-9);
    }
}

TEST_CASE("clear() returns the state to zero; the caller's zeroed buffer completes it") {
    std::vector<double> buf(64, 0.0);
    MoorerAllpass a; a.attach(buf.data(), buf.size()); a.setDelay(13); a.setGain(0.7);
    for (int k = 0; k < 100; ++k) a.tick(k == 0 ? 1.0 : 0.0);
    std::fill(buf.begin(), buf.end(), 0.0);
    a.clear();
    std::vector<double> fbuf(64, 0.0);
    MoorerAllpass f; f.attach(fbuf.data(), fbuf.size()); f.setDelay(13); f.setGain(0.7);
    for (int k = 0; k < 300; ++k) CHECK(a.tick(k == 0 ? 1.0 : 0.0) == f.tick(k == 0 ? 1.0 : 0.0));
}
