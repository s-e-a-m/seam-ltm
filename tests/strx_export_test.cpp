#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_export.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace Seam::strx;

namespace {

AnalysisFrame makeFrame(int bandCount = kNumBands) {
    AnalysisFrame fr;
    fr.bandCount   = bandCount;
    fr.bandSeconds = 62.4f;
    for (int i = 0; i < kNumBands; ++i) {
        fr.bandComp[i]   = (float)(i - 15) * 0.1f;
        fr.bandLevel[i]  = -50.0f + (float)i * 0.25f;
        fr.bandSettle[i] = (i < 3) ? 0.4f : 1.0f;
    }
    return fr;
}

ExportContext makeContext() {
    ExportContext ctx;
    ctx.sampleRate = 48000.0;
    ctx.source     = "multipink - STONE 1 - slot 4-7 - -23.0 dB";
    ctx.stoneId    = 1;
    ctx.when.tm_year = 126; ctx.when.tm_mon = 7; ctx.when.tm_mday = 18;
    ctx.when.tm_hour = 16; ctx.when.tm_min = 4; ctx.when.tm_sec = 12;
    return ctx;
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int countBandRows(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    bool inTable = false;
    int rows = 0;
    while (std::getline(in, line)) {
        if (line.rfind("band ", 0) == 0) { inTable = true; continue; }
        if (!inTable) continue;
        if (line.empty() || line[0] == '#') continue;
        ++rows;
    }
    return rows;
}

} // namespace

TEST_CASE("the file carries the context, not only the numbers") {
    const std::string t = formatBandTable(makeFrame(), makeContext());
    CHECK(contains(t, "saved:        2026-08-18 16:04:12"));
    CHECK(contains(t, "sample-rate:  48000 Hz"));
    CHECK(contains(t, "multipink - STONE 1 - slot 4-7 - -23.0 dB"));
    CHECK(contains(t, "integration:  62.4 s"));
    // The two fields the operator fills in. They must be PRESENT and EMPTY:
    // a folder of tables with no position recorded is the failure this whole
    // export exists to prevent, and a missing field cannot be noticed.
    CHECK(contains(t, "position:     \n"));
    CHECK(contains(t, "note:         \n"));
}

TEST_CASE("every measurable band gets a row, and no others") {
    CHECK(countBandRows(formatBandTable(makeFrame(kNumBands), makeContext())) == kNumBands);
    CHECK(countBandRows(formatBandTable(makeFrame(30), makeContext())) == 30);
    // ...and the missing one is explained rather than silently absent.
    CHECK(contains(formatBandTable(makeFrame(30), makeContext()), "withheld"));
    CHECK_FALSE(contains(formatBandTable(makeFrame(kNumBands), makeContext()), "withheld"));
}

TEST_CASE("the numbers reach the file with their sign and their settling") {
    const std::string t = formatBandTable(makeFrame(), makeContext());
    // Parse the first data row rather than matching column spacing, which is a
    // property of the layout and not of the measurement.
    std::istringstream in(t);
    std::string line;
    while (std::getline(in, line) && line.rfind("band ", 0) != 0) {}
    REQUIRE(std::getline(in, line));
    std::istringstream row(line);
    std::string name; double hz, comp, level, settled;
    REQUIRE((row >> name >> hz >> comp >> level >> settled));
    CHECK(name == "20");                                   // the ISO nominal name
    // Two decimals, because at one the exact centre 19.95 prints as "20.0" and
    // the column that distinguishes it from the nominal name stops doing so.
    CHECK(hz == doctest::Approx(19.95).epsilon(1e-3));
    CHECK(comp == doctest::Approx(-1.5));
    CHECK(level == doctest::Approx(-50.0));
    CHECK(settled == doctest::Approx(0.40));
    CHECK(contains(t, "RAISE"));      // the sign convention, spelled out in words
}

TEST_CASE("a write to a missing directory fails in words the operator can act on") {
    std::string path, error;
    const bool ok = writeBandTable("/no/such/volume/strx", makeFrame(), makeContext(),
                                    &path, &error);
    CHECK_FALSE(ok);
    CHECK(contains(error, "/no/such/volume/strx"));
    CHECK(contains(error, "volume not mounted"));
}

TEST_CASE("a successful write puts exactly the formatted text on disk") {
    const std::string dir = ".";
    std::string path, error;
    REQUIRE(writeBandTable(dir, makeFrame(), makeContext(), &path, &error));
    CHECK(contains(path, "2026-08-18-160412-stone1.txt"));
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::stringstream ss; ss << in.rdbuf();
    in.close();
    CHECK(ss.str() == formatBandTable(makeFrame(), makeContext()));
    std::remove(path.c_str());
}

TEST_CASE("an undeclared STONE is named as such rather than numbered zero") {
    ExportContext ctx = makeContext();
    ctx.stoneId = 0;
    std::string path, error;
    REQUIRE(writeBandTable(".", makeFrame(), ctx, &path, &error));
    CHECK(contains(path, "stone-undeclared"));
    std::remove(path.c_str());
}

TEST_CASE("a table with nothing behind it is not written as zeros") {
    // The first file this export ever wrote said "0 of 31 measurable" and then
    // printed 31 rows of +0.0 dB: a flawless measurement of nothing. A reader
    // skimming for the shape of the curve would have believed it.
    AnalysisFrame empty;                       // never touched by the analyser
    const std::string t = formatBandTable(empty, makeContext());
    CHECK(countBandRows(t) == 0);
    CHECK(contains(t, "NO MEASUREMENT"));
    CHECK_FALSE(contains(t, "+0.0"));

    AnalysisFrame stalled = makeFrame();       // bands known, but no time yet
    stalled.bandSeconds = 0.f;
    CHECK(countBandRows(formatBandTable(stalled, makeContext())) == 0);
}
