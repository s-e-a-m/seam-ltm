// The sheet grammar: pure formatter, links nothing but doctest.
// Timezone: event() prints LOCAL time, so the test pins TZ=UTC before use.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub_sheet.h"

#include <cstdlib>
#include <cstring>

using namespace Seam::hubsheet;

namespace {
struct Tz { Tz() { setenv("TZ", "UTC", 1); tzset(); } } g_tz;
}

TEST_CASE("header") {
    std::tm tm{};
    tm.tm_year = 125; tm.tm_mon = 10; tm.tm_mday = 25; tm.tm_hour = 14; tm.tm_min = 30; tm.tm_sec = 2;
    CHECK(header(tm, 4711) ==
          "# SEAM hub sheet\n# started: 2025-11-25 14:30:02   pid: 4711   hub: 1\n");
}

TEST_CASE("who") { CHECK(who("strx", 1) == "strx#1"); }

TEST_CASE("event aligns who in 12 and verb in 8") {
    // 2025-11-25 14:32:07.412 UTC
    const int64_t ms = 1764081127412LL;
    CHECK(event(ms, "strx#1", "says", "band-table #12") ==
          "14:32:07.412  strx#1        says      band-table #12");
    CHECK(event(ms, "multipink#1", "state", "pink") ==
          "14:32:07.412  multipink#1   state     pink");
    CHECK(event(ms, "hub", "sheet", "x") == "14:32:07.412  hub           sheet     x");
}

TEST_CASE("a who longer than 12 is kept whole, then two spaces") {
    CHECK(event(1764081127412LL, "averylongplugin#12", "leaves", "") ==
          "14:32:07.412  averylongplugin#12  leaves");
    CHECK(event(1764081127412LL, "averylongplugin#12", "says", "o") ==
          "14:32:07.412  averylongplugin#12  says      o");
}

TEST_CASE("band table rows, with a withheld band") {
    SeamHubBandTable t{};
    t.bandCount = 29;
    for (int i = 0; i < 31; ++i) t.bands[i] = {19.95 * (i + 1), 3.0, -41.2, 1.0};
    t.bands[1] = {39.9, -0.04, 12.34, 0.5};
    const std::string r = rows(t);
    size_t lines = 0;
    for (char c : r) lines += (c == '\n');
    CHECK(lines == 30);   // title + 29
    CHECK(r.rfind("  | band  hz        comp_db  level_db  settled\n", 0) == 0);
    CHECK(r.find("  | 20    19.95     +3.0     -41.2     1.00\n") != std::string::npos);
    CHECK(r.find("  | 40    39.90     -0.0     12.3      0.50\n") != std::string::npos);
    CHECK(r.find("  | 580") == std::string::npos);   // band 30 withheld
}

TEST_CASE("sections rows") {
    SeamHubSections p{};
    p.count = 2;
    p.sections[0] = {kSeamHubPeak, 0, 111.44, 2.0, -6.0};
    p.sections[1] = {kSeamHubHighShelf, 0, 8000.0, 0.71, 2.5};
    CHECK(rows(p) == "  | peak        111.4  2.00  -6.0\n"
                     "  | highshelf  8000.0  0.71  +2.5\n");
}

TEST_CASE("objects") {
    SeamHubBandTable t{};
    t.bandCount = 31; t.hasDecoder = 1; t.decoder.test = 1; t.decoder.cutHz = 160; t.integrationSec = 646.6;
    CHECK(tableObject(12, 1, t) == "band-table #12  stone 1  decoder W cut 160  31 bands  646.6 s");
    SeamHubSections p{};
    p.block = kSeamHubBlockCommon; p.count = 6; p.residualRmsDb = 0.8; p.residualMaxDb = 1.9; p.offsetDb = -3.2;
    CHECK(sectionsObject(13, p) == "sections #13  common  6  rms 0.8  max 1.9  offset -3.2");
    SeamHubState s{};
    s.kind = kSeamHubPink; s.stoneId = 1; s.levelDb = -23.0; s.active = 1;
    s.u.pink.slotStart = 0; s.u.pink.slotCount = 4;
    CHECK(stateObject(s) == "pink  stone 1  slots 0-3  -23.0 dB  active");
    CHECK(rows(s) == "");
}
