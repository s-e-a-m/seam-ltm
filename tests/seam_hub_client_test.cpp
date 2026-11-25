#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub_client.h"

#include <cstring>

// This test runs with SEAM_HUB_PATH pointing at the freshly built dylib
// (see tests/CMakeLists.txt), so the client must find and load it.
TEST_CASE("client loads the dylib and reports available") {
    auto& c = Seam::HubClient::instance();
    CHECK(c.available());
}

TEST_CASE("client round-trips a state through the real dylib") {
    auto& c = Seam::HubClient::instance();
    REQUIRE(c.available());

    int32_t h = c.registerMember("multipink", SEAM_HUB_BIT(kSeamHubPink), 0);
    REQUIRE(h >= 0);

    SeamHubState in{};
    in.kind    = kSeamHubPink;
    in.stoneId = 3;
    in.active  = 1;
    in.levelDb = -23.0;
    in.sampleRate = 48000.0;
    in.u.pink.slotStart = 8;
    in.u.pink.slotCount = 4;
    c.publishState(h, in);

    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    int32_t n = c.members(out, SEAM_HUB_MAX_MEMBERS);
    REQUIRE(n == 1);
    CHECK(std::strcmp(out[0].name, "multipink") == 0);
    CHECK(out[0].hasState == 1u);
    CHECK(out[0].state.kind == (uint32_t)kSeamHubPink);
    CHECK(out[0].state.stoneId == 3u);
    CHECK(out[0].state.u.pink.slotStart == 8);
    CHECK(out[0].state.u.pink.slotCount == 4);
    CHECK(out[0].state.levelDb == doctest::Approx(-23.0));

    c.unregisterMember(h);
    CHECK(c.members(out, SEAM_HUB_MAX_MEMBERS) == 0);
}

TEST_CASE("client posts a proposal and walks its lifecycle") {
    auto& c = Seam::HubClient::instance();
    REQUIRE(c.available());
    int32_t a = c.registerMember("strx", SEAM_HUB_BIT(kSeamHubBandTable), 0);
    int32_t b = c.registerMember("alogos", 0, SEAM_HUB_BIT(kSeamHubBandTable));
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);

    SeamHubProposal p{};
    p.type = kSeamHubBandTable;
    p.stoneId = 1;
    p.u.table.bandCount = 0;
    uint64_t id = c.post(a, p);
    REQUIRE(id != 0);
    CHECK(p.id == id);

    SeamHubProposal got[4];
    CHECK(c.proposals(0, got, 4) >= 1);
    CHECK(c.lifecycle(b, id) == kSeamHubArrived);
    c.transition(b, id, kSeamHubShown, false);
    CHECK(c.lifecycle(b, id) == kSeamHubShown);
    c.transition(b, id, kSeamHubApplied, true);
    CHECK(c.lifecycle(b, id) == kSeamHubApplied);

    c.unregisterMember(a);
    c.unregisterMember(b);
}

TEST_CASE("client reaches the sheet") {
    auto& c = Seam::HubClient::instance();
    REQUIRE(c.available());
    int32_t h = c.registerMember("x", 0, 0);
    SeamHubSheetStatus s = c.sheetStatus();
    CHECK(s.path[0] != '\0');
    c.newSheet();
    c.unregisterMember(h);
}

TEST_CASE("null mode: every call is a harmless no-op") {
    Seam::HubClient null = Seam::HubClient::makeUnavailableForTest();
    CHECK_FALSE(null.available());
    CHECK(null.registerMember("x", 1, 1) == SEAM_HUB_NO_HANDLE);

    SeamHubState s{};
    null.publishState(SEAM_HUB_NO_HANDLE, s);   // must not crash
    null.unregisterMember(SEAM_HUB_NO_HANDLE);

    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    CHECK(null.members(out, SEAM_HUB_MAX_MEMBERS) == 0);
    SeamHubProposal p{};
    CHECK(null.proposals(0, &p, 1) == 0);
    CHECK(null.lifecycle(0, 1) == kSeamHubNone);

    SeamHubSheetStatus st = null.sheetStatus();
    CHECK(st.path[0] == '\0');
    CHECK(st.writing == 0u);
    CHECK(st.pendingLines == 0u);
    CHECK(st.droppedLines == 0u);
    CHECK_FALSE(null.setSheetFolder("/tmp/x"));
    null.newSheet();
}

TEST_CASE("null mode refuses to post and transition") {
    Seam::HubClient null = Seam::HubClient::makeUnavailableForTest();
    SeamHubProposal p{};
    p.type = kSeamHubBandTable;
    CHECK(null.post(0, p) == 0);
    CHECK(p.id == 0);
    CHECK(null.transition(0, 1, kSeamHubApplied, true) == kSeamHubNone);
}
