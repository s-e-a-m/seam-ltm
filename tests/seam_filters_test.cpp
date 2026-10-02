#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_filters.h"
#include "delrm_signal.h"
#include "ref_windows.h"
#include "ref/seam_filters_ref.h"
#include <cmath>

using Seam::LeakyIntegrator;

static double runAgainst(double fs, const double* ref, const double* eref, double* energyErr) {
    LeakyIntegrator li;
    li.prepare(fs, 1.0);
    DelrmSignal sig(fs);
    refwin::Compare cmp(1, 16, (long)fs / 8);
    const int B = 256; double buf[B]; double* in[1] = { buf };
    const long total = (long)fs * 2;
    for (long pos = 0; pos < total; pos += B) {
        sig.fill(in, B, 1);
        for (int k = 0; k < B; ++k) cmp.see(0, pos + k, li.tick(buf[k]), ref);
    }
    CHECK(cmp.compared == 16 * 512);   // a test that compares nothing must fail
    *energyErr = cmp.energyRelErr(eref);
    return cmp.relErr();
}

TEST_CASE("LeakyIntegrator equals sfi.leakyint(1) at 96 and 48 kHz") {
    double e96, e48;
    const double r96 = runAgainst(96000.0, &filtersref::kLeaky96[0][0][0], &filtersref::kLeaky96_energy[0][0], &e96);
    const double r48 = runAgainst(48000.0, &filtersref::kLeaky48[0][0][0], &filtersref::kLeaky48_energy[0][0], &e48);
    MESSAGE("leakyint rel. error 96k " << r96 << ", 48k " << r48);
    CHECK(r96 < 1e-12);
    CHECK(r48 < 1e-12);
    CHECK(e96 < 1e-10);
    CHECK(e48 < 1e-10);
}

// The amplitude of the integral of sin(2 pi f t) is 1/(2 pi f) in seconds,
// at every rate, above fc.
static double sineGain(double fs, double f) {
    LeakyIntegrator li;
    li.prepare(fs, 1.0);
    const long n = (long)(fs * 12.0);      // 12 s: the 1 Hz pole settles
    double peak = 0.0;
    for (long i = 0; i < n; ++i) {
        const double y = li.tick(std::sin(2.0 * 3.141592653589793 * f * i / fs));
        if (i > n - (long)fs) peak = std::max(peak, std::fabs(y));
    }
    return peak;
}

TEST_CASE("above fc the gain is 1/(2 pi f), the same at 48 and 96 kHz") {
    for (double f : {29.7, 100.0, 1000.0}) {
        const double want = 1.0 / (2.0 * 3.141592653589793 * f);
        const double g96 = sineGain(96000.0, f), g48 = sineGain(48000.0, f);
        CAPTURE(f);
        CHECK(std::fabs(g96 / want - 1.0) < 2e-3);   // the leak's own 1/sqrt(1+(fc/f)^2)
        // 3e-3: the peak of a sampled sine at 48 kHz is off by up to 1-cos(pi*f/fs) (2e-3 at 1 kHz), not the integrator's doing.
        CHECK(std::fabs(g48 / g96 - 1.0) < 3e-3);
    }
}

TEST_CASE("a DC input stays bounded near 1/(2 pi fc)") {
    LeakyIntegrator li;
    li.prepare(96000.0, 1.0);
    double y = 0.0;
    for (long i = 0; i < 96000L * 10; ++i) y = li.tick(1.0);
    CHECK(std::fabs(y * 2.0 * 3.141592653589793 - 1.0) < 1e-2);
}

TEST_CASE("reset() forgets the state") {
    LeakyIntegrator li;
    li.prepare(96000.0, 1.0);
    for (int i = 0; i < 1000; ++i) li.tick(1.0);
    li.reset();
    CHECK(li.tick(0.0) == 0.0);
}
