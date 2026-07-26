#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_state.h"

#include "public.sdk/source/common/memorystream.h"

using Steinberg::IBStream;
using Steinberg::IBStreamer;
using Steinberg::MemoryStream;

// Sentinel pre-load: proves that fields the decoder does not reach keep the
// caller's defaults — never stack garbage. 0.777 is outside every value any
// test writes, so an overwrite is always detectable.
static constexpr double kSentinel = 0.777;

static void rewindStream(MemoryStream& s) {
    Steinberg::int64 pos = 0;
    s.seek(0, IBStream::kIBSeekSet, &pos);
}

TEST_CASE("full round-trip: write N, read N") {
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        for (int i = 0; i < 6; ++i) w.writeDouble(0.1 * i);
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 6);
    for (int i = 0; i < 6; ++i)
        CHECK(values[i] == doctest::Approx(0.1 * i));
}

TEST_CASE("legacy short blob: 3 doubles into a 6-field reader") {
    // The real m2xhgr case: a pre-trim session saved yaw/pitch/roll only.
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(0.25);  // yaw
        w.writeDouble(0.50);  // pitch
        w.writeDouble(0.75);  // roll
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 3);
    CHECK(values[0] == doctest::Approx(0.25));
    CHECK(values[1] == doctest::Approx(0.50));
    CHECK(values[2] == doctest::Approx(0.75));
    CHECK(values[3] == kSentinel);   // trims keep the caller's defaults
    CHECK(values[4] == kSentinel);
    CHECK(values[5] == kSentinel);
}

TEST_CASE("empty stream: zero fields read, all defaults intact") {
    MemoryStream stream;

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 0);
    for (int i = 0; i < 6; ++i)
        CHECK(values[i] == kSentinel);
}

TEST_CASE("blob truncated mid-field: partial field is never applied") {
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(0.25);
        w.writeDouble(0.50);
        w.writeDouble(0.75);
        w.writeInt32(0xDEAD);   // 4 stray bytes — half of a double
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 3);
    CHECK(values[3] == kSentinel);   // the partial field stays default
    CHECK(values[4] == kSentinel);
    CHECK(values[5] == kSentinel);
}

TEST_CASE("longer blob than expected: read the first N, ignore the tail") {
    // The reverse migration: an older plugin build reading a newer state.
    MemoryStream stream;
    {
        IBStreamer w(&stream, kLittleEndian);
        for (int i = 0; i < 9; ++i) w.writeDouble(0.1 * i);
    }
    rewindStream(stream);

    double values[6] = { kSentinel, kSentinel, kSentinel,
                         kSentinel, kSentinel, kSentinel };
    CHECK(Seam::readStateDoubles(&stream, values, 6) == 6);
    for (int i = 0; i < 6; ++i)
        CHECK(values[i] == doctest::Approx(0.1 * i));
}
