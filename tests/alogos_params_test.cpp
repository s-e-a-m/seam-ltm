#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "alogos_params.h"
#include <cmath>
#include <limits>

using namespace alogos;

static const double kNaN = std::numeric_limits<double>::quiet_NaN();

TEST_CASE("defaults: POWER off, id 0, 3.33 m, 17 sections, phi, D = 1, g = 1/phi") {
    ParamBox box;
    const Plain p = box.plain();
    CHECK_FALSE(p.power);
    CHECK(p.stoneId == 0);
    CHECK(p.metres == doctest::Approx(kDefaultMetres).epsilon(1e-12));
    CHECK(p.sections == kDefaultSections);
    CHECK(p.sections == 17);
    CHECK(p.scale == (int)Scale::Phi);
    CHECK(p.density == 0);
    CHECK(p.gain == (int)Gain::InvPhi);
}

TEST_CASE("every value survives normalized and back") {
    for (int id = 0; id <= kMaxStoneId; ++id) CHECK(normalizedToStoneId(stoneIdToNormalized(id)) == id);
    for (int n = 1; n <= kMaxSections; ++n) CHECK(normalizedToSections(sectionsToNormalized(n)) == n);
    for (int i = 0; i <= 3; ++i) CHECK(normalizedToScale(scaleToNormalized(i)) == i);
    for (int i = 0; i <= 2; ++i) CHECK(normalizedToDensity(densityToNormalized(i)) == i);
    for (int i = 0; i <= 4; ++i) CHECK(normalizedToGain(gainToNormalized(i)) == i);
    for (int k = 0; k <= 500; ++k) {
        const double m = 1.0 + k * 0.01;
        CHECK(normalizedToDistance(distanceToNormalized(m)) == doctest::Approx(m).epsilon(1e-12));
    }
}

TEST_CASE("step counts: id 9 values, sections 128, scale 4, density 3, g 5") {
    CHECK(kStoneIdSteps + 1 == 9);
    CHECK(kSectionsSteps + 1 == 128);
    CHECK(kScaleSteps + 1 == 4);
    CHECK(kDensitySteps + 1 == 3);
    CHECK(kGainSteps + 1 == 5);
    CHECK(sectionsToNormalized(1) == 0.0);
    CHECK(sectionsToNormalized(kMaxSections) == 1.0);
    CHECK(distanceToNormalized(1.0) == 0.0);
    CHECK(distanceToNormalized(6.0) == 1.0);
    CHECK(distanceToNormalized(3.5) == doctest::Approx(0.5));
}

TEST_CASE("off-grid host values map as the SDK's FromNormalized") {
    for (int k = 0; k <= 1000; ++k) {
        const double norm = k / 1000.0;
        CHECK(normalizedToStoneId(norm) == std::min(8, (int)(norm * 9.0)));
        CHECK(normalizedToSections(norm) == 1 + std::min(127, (int)(norm * 128.0)));
        CHECK(normalizedToScale(norm) == std::min(3, (int)(norm * 4.0)));
        CHECK(normalizedToDensity(norm) == std::min(2, (int)(norm * 3.0)));
        CHECK(normalizedToGain(norm) == std::min(4, (int)(norm * 5.0)));
    }
    CHECK(normalizedToStoneId(0.56) == 5);    // rounding would give 4
    CHECK(normalizedToScale(0.5) == 2);
    CHECK(normalizedToDensity(0.5) == 1);
    CHECK(normalizedToGain(0.5) == 2);
}

TEST_CASE("out-of-range recalled values clamp (scale 7, density 3, g 9, 40 m, sections 0 and 500)") {
    ParamBox box;
    box.store(Param::StoneId, stoneIdToNormalized(9));
    box.store(Param::Distance, distanceToNormalized(40.0));
    box.store(Param::Sections, sectionsToNormalized(500));
    box.store(Param::Scale, scaleToNormalized(7));
    box.store(Param::Density, densityToNormalized(3));
    box.store(Param::Gain, gainToNormalized(9));
    Plain p = box.plain();
    CHECK(p.stoneId == kMaxStoneId);
    CHECK(p.metres == kMaxMetres);
    CHECK(p.sections == kMaxSections);
    CHECK(p.scale == 3);
    CHECK(p.density == 2);
    CHECK(p.gain == 4);

    box.store(Param::StoneId, -3.0);
    box.store(Param::Distance, distanceToNormalized(-1.0));
    box.store(Param::Sections, sectionsToNormalized(0));
    box.store(Param::Scale, scaleToNormalized(-7));
    box.store(Param::Density, -0.5);
    box.store(Param::Gain, -9.0);
    p = box.plain();
    CHECK(p.stoneId == 0);
    CHECK(p.metres == kMinMetres);
    CHECK(p.sections == 1);
    CHECK(p.scale == 0);
    CHECK(p.density == 0);
    CHECK(p.gain == 0);

    box.store(Param::Distance, 1e300);
    CHECK(box.plain().metres == kMaxMetres);
    box.store(Param::Distance, -std::numeric_limits<double>::infinity());
    CHECK(box.plain().metres == kMinMetres);
}

TEST_CASE("NaN falls back to the defaults") {
    ParamBox box;
    for (int i = 1; i < kNumParams; ++i) box.store((Param)i, kNaN);
    const Plain p = box.plain();
    CHECK(p.stoneId == 0);
    CHECK(p.metres == kDefaultMetres);
    CHECK(p.sections == kDefaultSections);
    CHECK(p.scale == 0);
    CHECK(p.density == 0);
    CHECK(p.gain == 0);
}

TEST_CASE("sections above N_max survive a round trip of scale (ParamBox does not clamp to N_max)") {
    ParamBox box;
    box.store(Param::Sections, sectionsToNormalized(17));
    box.store(Param::Scale, scaleToNormalized((int)Scale::Pi));
    box.store(Param::Distance, distanceToNormalized(6.0));
    CHECK(box.plain().sections == 17);
    box.store(Param::Scale, scaleToNormalized((int)Scale::Phi));
    CHECK(box.plain().sections == 17);
    CHECK(box.plain().scale == 0);
    box.store(Param::Sections, sectionsToNormalized(128));
    box.store(Param::Scale, scaleToNormalized((int)Scale::Pi));
    CHECK(box.plain().sections == 128);
}

TEST_CASE("plain values feed Engine::setSettings unchanged") {
    ParamBox box;
    box.store(Param::StoneId, stoneIdToNormalized(3));
    box.store(Param::Distance, distanceToNormalized(2.5));
    box.store(Param::Scale, scaleToNormalized(2));
    box.store(Param::Density, densityToNormalized(2));
    box.store(Param::Gain, gainToNormalized(4));
    const Plain p = box.plain();
    CHECK(p.metres == doctest::Approx(2.5));
    CHECK(p.scale == (int)Scale::E);
    CHECK(p.density == 2);
    CHECK(p.gain == (int)Gain::Diffusion);
}
