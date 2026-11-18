#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "bastooge_state.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/common/memorystream.h"
#include <cmath>
#include <initializer_list>

using namespace Seam::bastooge;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

// A blob as a host stores it: little-endian doubles, in field order.
static void writeBlob(MemoryStream& s, std::initializer_list<double> fields) {
    Steinberg::IBStreamer w(&s, kLittleEndian);
    for (double v : fields) w.writeDouble(v);
    rewindStream(s);
}

// A box that holds the opposite of every default, so a recall that left a
// field alone would show.
static void dirty(ParamBox& b) {
    b.storeCut(111.0);
    b.storeTest(Test::Z);
    b.storeBypass(true);
}

TEST_CASE("a full blob [250, 2, 1] restores cut 250, test X, bypass on") {
    MemoryStream s;
    writeBlob(s, {250.0, 2.0, 1.0});
    ParamBox b;
    CHECK(readState(&s, b) == 3);
    CHECK(b.cut() == 250.0);
    CHECK(b.test() == Test::X);
    CHECK(b.bypass());
}

TEST_CASE("an empty blob restores the defaults: 160 Hz, Normal, bypass off") {
    MemoryStream s;
    ParamBox b; dirty(b);
    CHECK(readState(&s, b) == 0);
    CHECK(b.cut() == 160.0);
    CHECK(b.test() == Test::Normal);
    CHECK_FALSE(b.bypass());
}

TEST_CASE("a one-field blob [125] restores 125 Hz and the defaults after it") {
    MemoryStream s;
    writeBlob(s, {125.0});
    ParamBox b; dirty(b);
    CHECK(readState(&s, b) == 1);
    CHECK(b.cut() == 125.0);
    CHECK(b.test() == Test::Normal);
    CHECK_FALSE(b.bypass());
}

TEST_CASE("out-of-range values are clamped: cut 1000 restores 250, test 9 restores Normal") {
    MemoryStream s;
    writeBlob(s, {1000.0, 9.0, 0.0});
    ParamBox b; dirty(b);
    CHECK(readState(&s, b) == 3);
    CHECK(b.cut() == 250.0);
    CHECK(b.test() == Test::Normal);
    CHECK_FALSE(b.bypass());
}

TEST_CASE("NaN and negative fields restore the defaults") {
    MemoryStream s;
    writeBlob(s, {std::nan(""), -1.0, std::nan("")});
    ParamBox b; dirty(b);
    CHECK(readState(&s, b) == 3);
    CHECK(b.cut() == 160.0);
    CHECK(b.test() == Test::Normal);
    CHECK_FALSE(b.bypass());
}

TEST_CASE("the state round-trips") {
    ParamBox a;
    a.storeCut(201.5);
    a.storeTest(Test::Y);
    a.storeBypass(true);
    MemoryStream s;
    writeState(&s, a);
    rewindStream(s);
    ParamBox b;
    CHECK(readState(&s, b) == 3);
    CHECK(b.cut() == 201.5);
    CHECK(b.test() == Test::Y);
    CHECK(b.bypass());
}

TEST_CASE("the cut maps logarithmically: 0 -> 100 Hz, 1 -> 250 Hz, the default round-trips") {
    CHECK(cutFromNormalized(0.0) == doctest::Approx(100.0));
    CHECK(cutFromNormalized(1.0) == doctest::Approx(250.0));
    CHECK(cutFromNormalized(0.5) == doctest::Approx(100.0 * std::sqrt(2.5)));
    CHECK(cutFromNormalized(cutToNormalized(160.0)) == doctest::Approx(160.0));
}

TEST_CASE("the Test list maps its five entries and clamps what lies outside") {
    CHECK(testFromNormalized(0.0) == Test::Normal);
    CHECK(testFromNormalized(0.25) == Test::W);
    CHECK(testFromNormalized(0.5) == Test::X);
    CHECK(testFromNormalized(0.75) == Test::Y);
    CHECK(testFromNormalized(1.0) == Test::Z);
    CHECK(testFromNormalized(1.5) == Test::Normal);
    CHECK(testFromNormalized(std::nan("")) == Test::Normal);
    for (int k = 0; k <= 4; ++k) CHECK(testFromNormalized(testToNormalized((Test)k)) == (Test)k);
}
