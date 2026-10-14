#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lmo_params.h"
#include <cmath>

using namespace lmo;

TEST_CASE("normalized values become plain values on the parameter ranges") {
    ParamBox box;
    box.store(Param::Frequency, 0.5);
    box.store(Param::Glide, 0.4);
    box.store(Param::Delta, 1.0);
    box.store(Param::Volume, 0.25);
    box.store(Param::Power, 1.0);
    const Plain p = box.plain();
    // the logarithmic taper: half the travel is the geometric mean, Bb2
    CHECK(std::fabs(p.f - std::sqrt(kFMin * kFMax)) < 1e-9);
    CHECK(std::fabs(p.f - 442.0 * std::pow(2.0, -23.0 / 12.0)) < 1e-9);
    CHECK(std::fabs(p.glide - 0.4 * kGlideMax) < 1e-12);
    CHECK(p.delta == kDeltaMax);
    CHECK(p.volume == 0.25);
    CHECK(p.power);
}

TEST_CASE("defaults: f at cue 0, everything else at rest") {
    ParamBox box;
    const Plain p = box.plain();
    CHECK(std::fabs(p.f - kFDefault) < 1e-12);
    CHECK(p.glide == 0.0);
    CHECK(p.delta == 0.0);
    CHECK(p.volume == 0.0);
    CHECK_FALSE(p.power);
    CHECK(box.normalized(Param::Frequency) == defaultNormalized(Param::Frequency));
}

TEST_CASE("f: Bb0 to Bb4 at A = 442, equal intervals per fader step") {
    CHECK(std::fabs(kFMin - 442.0 * std::pow(2.0, -47.0 / 12.0)) < 1e-12);
    CHECK(std::fabs(kFMax / kFMin - 16.0) < 1e-12);         // four octaves
    // a 7-bit fader: every step is 48/127 of a semitone
    for (int cc = 0; cc < 127; ++cc) {
        const double st = 12.0 * std::log2(fFromNormalized((cc + 1) / 127.0)
                                           / fFromNormalized(cc / 127.0));
        CHECK(std::fabs(st - 48.0 / 127.0) < 1e-9);
    }
    for (double hz : { kFMin, 97.44, 200.0, kFMax })
        CHECK(std::fabs(fFromNormalized(fToNormalized(hz)) - hz) < 1e-9);
}

TEST_CASE("recall while playing: no interleaving starts f on the old glide") {
    // Playing: glide 120 s, f at 97.44. A preset recalls glide 0, f 200.
    // The UI thread stores the five values one by one while process() may
    // read the box between any two stores. Whatever the point, f must reach
    // 200 Hz in 25 ms, never on a 120 s glide.
    const double fs = 48000.0;
    const double recalled[kNumParams] = {
        1.0, fToNormalized(200.0), 0.0, 0.0, 1.0 };
    for (int cut = 0; cut <= kNumParams; ++cut) {
        ParamBox box;
        box.store(Param::Power, 1.0);
        box.store(Param::Volume, 1.0);
        box.store(Param::Glide, 120.0 / kGlideMax);
        box.store(Param::Frequency, fToNormalized(97.44));
        Engine e; e.prepare(fs);
        applyTo(box.plain(), e); e.reset();

        // the recall, interrupted after `cut` stores by one process() block
        for (int k = 0; k < kNumParams; ++k) {
            if (k == cut) { applyTo(box.plain(), e); }
            box.store(kRecallOrder[k], recalled[(int)kRecallOrder[k]]);
        }
        applyTo(box.plain(), e);

        double buf[4][1200]; double* out[4] = { buf[0], buf[1], buf[2], buf[3] };
        e.process(out, 1200);                 // 25 ms at 48 kHz
        // 200 Hz through the log taper and back: equal to 1e-12, not bit for bit
        CHECK_MESSAGE(std::fabs(e.currentFrequency() - 200.0) < 1e-9, "interrupted after store ", cut);
    }
}
