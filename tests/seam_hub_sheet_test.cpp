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
    t.bands[2] = {31.62, 0.0, -40.0, 1.0};
    t.bands[18] = {1258.93, 1.25, -30.0, 0.9};
    const std::string r2 = rows(t);
    CHECK(r2.find("  | 31.5  31.62     +0.0     -40.0     1.00\n") != std::string::npos);
    CHECK(r2.find("  | 1.25k 1258.93   +1.2     -30.0     0.90\n") != std::string::npos);
    CHECK(r.find("  | 25    39.90     -0.0     12.3      0.50\n") != std::string::npos);
    CHECK(r.find("  | 20k") == std::string::npos);   // band 30 withheld
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
    t.sampleRate = 48000; t.sourceLevelDb = -23.0; t.emitterKind = kSeamHubPink; t.emitterSlots = 4;
    t.decoder.commonId = 9; t.decoder.stoneOfModel = 1;
    std::strcpy(t.source, "multipink#1 slots 0-3\nsecond line");
    CHECK(tableObject(12, 1, t) ==
          "band-table #12  stone 1  decoder test W  cut 160  bypass off  model 1  common #9  difference -"
          "  piece off  sub off 0 Hz  31 bands  646.6 s  48000 Hz  level -23.0 dB  emitter pink slots 4"
          "  source \"multipink#1 slots 0-3 second line\"");
    SeamHubBandTable bare{};
    CHECK(tableObject(3, 0, bare) == "band-table #3  stone 0  0 bands  0.0 s  0 Hz  level 0.0 dB  emitter none");
    bare.bandCount = 99;   // clamped to the 31 bands a table has
    CHECK(tableObject(3, 0, bare).find("  31 bands") != std::string::npos);
    std::memset(bare.source, 'x', sizeof bare.source);   // no NUL: bounded by the field
    CHECK(tableObject(3, 0, bare).find("source \"" + std::string(128, 'x') + "\"") != std::string::npos);

    SeamHubSections p{};
    p.block = kSeamHubBlockCommon; p.count = 6; p.residualRmsDb = 0.8; p.residualMaxDb = 1.9; p.offsetDb = -3.2;
    p.fitLoHz = 63; p.fitHiHz = 16000; p.roomLimitHz = 200; p.fromIds[0] = 12;
    CHECK(sectionsObject(13, p) ==
          "sections #13  common  6  rms 0.8  max 1.9  offset -3.2  fit 63-16000 Hz  room 200 Hz  from #12");
    SeamHubSections d{};
    d.block = kSeamHubBlockDifference; d.count = 2; d.fitLoHz = 160; d.fitHiHz = 2000;
    d.fromIds[0] = 14; d.fromIds[2] = 15;
    CHECK(sectionsObject(16, d) ==
          "sections #16  difference  2  rms 0.0  max 0.0  offset +0.0  fit 160-2000 Hz  room 0 Hz  from #14 #15");
    d.fromIds[0] = d.fromIds[2] = 0;
    CHECK(sectionsObject(16, d).substr(sectionsObject(16, d).size() - 8) == "  from -");

    SeamHubState s{};
    s.kind = kSeamHubPink; s.stoneId = 1; s.levelDb = -23.0; s.active = 1; s.sampleRate = 48000;
    s.u.pink.slotStart = 0; s.u.pink.slotCount = 4;
    CHECK(stateObject(s) == "pink  stone 1  slots 0-3  -23.0 dB  active  48000 Hz");
    CHECK(rows(s) == "");
    s.u.pink.slotStart = -1; s.u.pink.slotCount = 0; s.active = 0;
    CHECK(stateObject(s) == "pink  stone 1  slots -1+0  -23.0 dB  idle  48000 Hz");
}

TEST_CASE("a glide state prints every field the writer compares") {
    SeamHubState s{};
    s.kind = kSeamHubGlide; s.stoneId = 2; s.levelDb = -18.0; s.active = 1; s.sampleRate = 96000;
    s.u.glide.passCounter = 7; s.u.glide.passStartSample = 123456789;
    s.u.glide.f0 = 20; s.u.glide.f1 = 20000; s.u.glide.durationSec = 30; s.u.glide.deltaSec = -0.25;
    s.u.glide.sweepMode = 1; s.u.glide.diracMode = 2;
    CHECK(stateObject(s) == "glide  stone 2  pass 7 start 123456789  -18.0 dB  active  96000 Hz");
    CHECK(rows(s) == "  | 20 .. 20000 Hz  T 30 s  delta -0.25 s  sweep 1  dirac 2\n");
    s.u.glide.passStartSample = -1;
    CHECK(stateObject(s).find("pass 7 start -1") != std::string::npos);
}

TEST_CASE("a decoder state prints every field the writer compares") {
    SeamHubState s{};
    s.kind = kSeamHubDecoder; s.stoneId = 1; s.levelDb = 0; s.active = 1; s.sampleRate = 48000;
    SeamHubDecoder& d = s.u.decoder;
    d.test = 2; d.bypass = 1; d.stoneOfModel = 3; d.cutHz = 344; d.commonId = 13; d.differenceId = 21;
    d.pieceOn = 1; d.subOn = 1; d.subHz = 142;
    CHECK(stateObject(s) ==
          "decoder  stone 1  test X  cut 344  bypass on  model 3  common #13  difference #21"
          "  piece on  sub on 142 Hz  0.0 dB  active  48000 Hz");
    CHECK(rows(s) == "");
    s.kind = 9;   // an unknown kind: the common fields only
    CHECK(stateObject(s) == "unknown  stone 1  0.0 dB  active  48000 Hz");
}

TEST_CASE("section words") {
    SeamHubSections p{};
    p.count = 3;
    p.sections[0] = {kSeamHubLowShelf, 0, 65.5, 0.71, -15.0};
    p.sections[1] = {kSeamHubPeak, 0, 1516.0, 4.0, 3.0};
    p.sections[2] = {99, 0, 1000.0, 1.0, 0.0};
    CHECK(rows(p) == "  | lowshelf     65.5  0.71  -15.0\n"
                     "  | peak       1516.0  4.00  +3.0\n"
                     "  | unknown    1000.0  1.00  +0.0\n");
    p.count = 99;   // clamped to SEAM_HUB_MAX_SECTIONS rows
    size_t lines = 0;
    for (char c : rows(p)) lines += (c == '\n');
    CHECK(lines == SEAM_HUB_MAX_SECTIONS);
}
