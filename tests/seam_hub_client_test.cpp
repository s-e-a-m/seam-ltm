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
    // Not 0: nothing was recorded, and 0 is the hub's success.
    CHECK(null.transition(0, 1, kSeamHubApplied, true) == SEAM_HUB_ERR_NO_HUB);
    CHECK(null.transition(0, 1, kSeamHubShown, false, "x") == SEAM_HUB_ERR_NO_HUB);
    CHECK(null.lifecycleOf(0, 1, 1) == kSeamHubNone);
    CHECK(null.recentLines().empty());
}

TEST_CASE("a receiver without a handle cannot transition, hub or not") {
    auto& c = Seam::HubClient::instance();
    REQUIRE(c.available());
    CHECK(c.transition(SEAM_HUB_NO_HANDLE, 1, kSeamHubShown, false) == SEAM_HUB_ERR_NO_HUB);
}

TEST_CASE("the client remembers the instance it was given and asks for it back") {
    auto& c = Seam::HubClient::instance();
    REQUIRE(c.available());
    uint32_t first = 0, second = 0;
    const int32_t a = c.registerMember("strx", 0, 0, first);
    const int32_t b = c.registerMember("strx", 0, 0, second);
    REQUIRE(a != SEAM_HUB_NO_HANDLE);
    REQUIRE(b != SEAM_HUB_NO_HANDLE);
    CHECK(first == 1u);
    CHECK(second == 2u);
    c.unregisterMember(a);
    c.unregisterMember(b);
    // The second one comes back first: it is still #2.
    const int32_t b2 = c.registerMember("strx", 0, 0, second);
    CHECK(second == 2u);
    const int32_t a2 = c.registerMember("strx", 0, 0, first);
    CHECK(first == 1u);
    c.unregisterMember(a2);
    c.unregisterMember(b2);
}

TEST_CASE("the client reads the recent lines") {
    auto& c = Seam::HubClient::instance();
    REQUIRE(c.available());
    const int32_t h = c.registerMember("recent", 0, 0);
    const std::string r = c.recentLines();
    CHECK(r.find("recent#1      arrives") != std::string::npos);
    c.unregisterMember(h);
}
