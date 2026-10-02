#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_compressors.h"
#include "delrm_signal.h"
#include "ref_windows.h"
#include "ref/seam_compressors_ref.h"
#include <cmath>
#include <limits>

using Seam::CompressorMono;

struct Result { double out, gain, energy; long outCompared, gainCompared; };

// The reference DSP: *(4) <: compressor_mono(...), (compression_gain_mono(...) : linear2db).
static Result runAgainst(double ratio, double th, double att, double rel, const double* ref, const double* eref) {
    const double fs = 96000.0;
    CompressorMono c;
    c.prepare(fs, ratio, th, att, rel);
    DelrmSignal sig(fs);
    // Output 0 comes first in the reference's layout: a one-output Compare
    // reads its windows and its 16 energies; the gain is compared sample by
    // sample through a two-output Compare, by its absolute error in dB.
    refwin::Compare out(1, 16, (long)fs / 8), gain(2, 16, (long)fs / 8);
    const int B = 256; double buf[B]; double* in[1] = { buf };
    for (long pos = 0; pos < (long)fs * 2; pos += B) {
        sig.fill(in, B, 1);
        for (int k = 0; k < B; ++k) {
            const double y = c.tick(4.0 * buf[k]);
            out.see(0, pos + k, y, ref);
            gain.see(1, pos + k, c.gainDb(), ref);
        }
    }
    const double inf = std::numeric_limits<double>::infinity();
    return { out.relErr(), gain.nonFinite ? inf : gain.maxErr, out.energyRelErr(eref), out.compared, gain.compared };
}

TEST_CASE("CompressorMono equals co.compressor_mono(11, -24, 0.03, 0.04)") {
    const Result r = runAgainst(11, -24, 0.03, 0.04, &compressorsref::kComp96[0][0][0], &compressorsref::kComp96_energy[0][0]);
    MESSAGE("compressor rel. error " << r.out << ", gain abs. error " << r.gain << " dB");
    CHECK(r.outCompared == 16 * 512);
    CHECK(r.gainCompared == 16 * 512);
    CHECK(r.out < 1e-12);
    CHECK(r.gain < 1e-9);          // dB, near -40: relative ~1e-11
    CHECK(r.energy < 1e-10);
}

TEST_CASE("the library is generic: another ratio, threshold and times") {
    const Result r = runAgainst(4, -12, 0.005, 0.2, &compressorsref::kComp2_96[0][0][0], &compressorsref::kComp2_96_energy[0][0]);
    MESSAGE("compressor rel. error " << r.out << ", gain abs. error " << r.gain << " dB");
    CHECK(r.outCompared == 16 * 512);
    CHECK(r.gainCompared == 16 * 512);
    CHECK(r.out < 1e-12);
    CHECK(r.gain < 1e-9);
    CHECK(r.energy < 1e-10);
}

TEST_CASE("gainDb() is the gain the sample was multiplied by") {
    CompressorMono c;
    c.prepare(96000.0, 11, -24, 0.03, 0.04);
    for (int i = 0; i < 20000; ++i) {
        const double x = 0.5 * std::sin(i * 0.01);
        const double y = c.tick(x);
        if (std::fabs(x) > 1e-3) CHECK(std::fabs(y / x - std::pow(10.0, c.gainDb() / 20.0)) < 1e-12);
    }
}

TEST_CASE("silence gives exactly 0, never NaN, and the gain returns to 0 dB") {
    CompressorMono c;
    c.prepare(96000.0, 11, -24, 0.03, 0.04);
    for (int i = 0; i < 9600; ++i) c.tick(0.9);
    double y = 1.0;
    for (int i = 0; i < 96000 * 3; ++i) y = c.tick(0.0);
    CHECK(y == 0.0);
    CHECK(std::isfinite(c.gainDb()));
    CHECK(c.gainDb() > -1e-6);
}

TEST_CASE("tau2pole: 0 below epsilon, exp(-1/(tau fs)) otherwise") {
    CHECK(Seam::tau2pole(0.0, 96000.0) == 0.0);
    CHECK(std::fabs(Seam::tau2pole(0.03, 96000.0) - std::exp(-1.0 / (0.03 * 96000.0))) < 1e-16);
}
