#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "inphirev_params.h"
#include <cmath>
#include <limits>

using namespace inphirev;

TEST_CASE("defaults: POWER off, StoneId '?', grid 10, 17 sections") {
    ParamBox box;
    const Plain p = box.plain();
    CHECK_FALSE(p.power);
    CHECK(p.stoneId == 0);
    CHECK(p.grid == kDefaultGrid);
    CHECK(p.sections == kDefaultSections);
}

TEST_CASE("every value survives normalized and back") {
    for (int id = 0; id <= kMaxStoneId; ++id) CHECK(normalizedToStoneId(stoneIdToNormalized(id)) == id);
    for (int j = 0; j <= kGridMax; ++j) CHECK(normalizedToGrid(gridToNormalized(j)) == j);
    for (int n = 1; n <= kMaxSections; ++n) CHECK(normalizedToSections(sectionsToNormalized(n)) == n);
}

TEST_CASE("step counts: StoneId 9 values, Grid 15, Sections 18") {
    CHECK(kStoneIdSteps + 1 == 9);
    CHECK(kGridSteps + 1 == 15);
    CHECK(kSectionsSteps + 1 == 18);
    CHECK(stoneIdToNormalized(kMaxStoneId) == 1.0);
    CHECK(gridToNormalized(kGridMax) == 1.0);
    CHECK(sectionsToNormalized(1) == 0.0);
    CHECK(sectionsToNormalized(kMaxSections) == 1.0);
}

TEST_CASE("off-grid host values map as the SDK's stepped parameters display them") {
    for (int k = 0; k <= 1000; ++k) {
        const double norm = k / 1000.0;
        CHECK(normalizedToGrid(norm) == std::min(14, (int)(norm * 15.0)));
        CHECK(normalizedToSections(norm) == 1 + std::min(17, (int)(norm * 18.0)));
        CHECK(normalizedToStoneId(norm) == (int)(norm * 8 + 0.5));
    }
}

TEST_CASE("out-of-range recalled values clamp to the table (id 9, j 20, sections 0 and 40)") {
    ParamBox box;
    box.store(Param::StoneId, stoneIdToNormalized(9));
    box.store(Param::Grid, gridToNormalized(20));
    box.store(Param::Sections, sectionsToNormalized(0));
    Plain p = box.plain();
    CHECK(p.stoneId == kMaxStoneId);
    CHECK(p.grid == kGridMax);
    CHECK(p.sections == 1);
    box.store(Param::Sections, sectionsToNormalized(40));
    box.store(Param::StoneId, -3.0);
    box.store(Param::Grid, -0.5);
    p = box.plain();
    CHECK(p.sections == kMaxSections);
    CHECK(p.stoneId == 0);
    CHECK(p.grid == 0);
}

TEST_CASE("NaN falls back to the defaults") {
    ParamBox box;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    box.store(Param::StoneId, nan);
    box.store(Param::Grid, nan);
    box.store(Param::Sections, nan);
    const Plain p = box.plain();
    CHECK(p.stoneId == 0);
    CHECK(p.grid == kDefaultGrid);
    CHECK(p.sections == kDefaultSections);
}
