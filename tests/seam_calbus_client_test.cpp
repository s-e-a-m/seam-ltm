#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_calbus_client.h"

// This test runs with SEAM_CALBUS_PATH pointing at the freshly built dylib
// (see tests/CMakeLists.txt), so the client must find and load it.
TEST_CASE("client loads the dylib and reports available") {
    auto& c = Seam::CalbusClient::instance();
    CHECK(c.available());
}

TEST_CASE("client round-trips a record through the real dylib") {
    auto& c = Seam::CalbusClient::instance();
    REQUIRE(c.available());

    int32_t h = c.registerSlot();
    CHECK(h >= 0);

    SeamCalbusRecord in{};
    in.kind    = kSeamCalbusPink;
    in.stoneId = 3;
    in.active  = 1;
    in.levelDb = -23.0;
    in.sampleRate = 48000.0;
    in.u.pink.slotStart = 8;
    in.u.pink.slotCount = 4;
    c.publish(h, in);

    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    int32_t n = c.snapshot(out, SEAM_CALBUS_MAX_SLOTS);
    CHECK(n == 1);
    CHECK(out[0].kind == (uint32_t)kSeamCalbusPink);
    CHECK(out[0].stoneId == 3u);
    CHECK(out[0].u.pink.slotStart == 8);
    CHECK(out[0].u.pink.slotCount == 4);
    CHECK(out[0].levelDb == doctest::Approx(-23.0));

    c.unregisterSlot(h);
    CHECK(c.snapshot(out, SEAM_CALBUS_MAX_SLOTS) == 0);
}

TEST_CASE("null mode: every call is a harmless no-op") {
    // Exercises the same code paths a plugin hits when the dylib is missing.
    Seam::CalbusClient null = Seam::CalbusClient::makeUnavailableForTest();
    CHECK_FALSE(null.available());
    CHECK(null.registerSlot() == -1);

    SeamCalbusRecord r{};
    null.publish(-1, r);           // must not crash
    null.unregisterSlot(-1);       // must not crash

    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(null.snapshot(out, SEAM_CALBUS_MAX_SLOTS) == 0);
}
