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
    CHECK(std::fabs(p.f - (kFMin + 0.5 * (kFMax - kFMin))) < 1e-12);
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

TEST_CASE("recall while playing: no interleaving starts f on the old glide") {
    // Playing: glide 120 s, f at 97.44. A preset recalls glide 0, f 200.
    // The UI thread stores the five values one by one while process() may
    // read the box between any two stores. Whatever the point, f must reach
    // 200 Hz in 25 ms, never on a 120 s glide.
    const double fs = 48000.0;
    const double recalled[kNumParams] = {
        1.0, (200.0 - kFMin) / (kFMax - kFMin), 0.0, 0.0, 1.0 };
    for (int cut = 0; cut <= kNumParams; ++cut) {
        ParamBox box;
        box.store(Param::Power, 1.0);
        box.store(Param::Volume, 1.0);
        box.store(Param::Glide, 120.0 / kGlideMax);
        box.store(Param::Frequency, (97.44 - kFMin) / (kFMax - kFMin));
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
        CHECK_MESSAGE(e.currentFrequency() == 200.0, "interrupted after store ", cut);
    }
}
