#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub_client.h"

// SEAM_HUB_PATH points at the version-2 stub (see tests/CMakeLists.txt).
TEST_CASE("client refuses a dylib with a mismatched version") {
    auto& c = Seam::HubClient::instance();
    CHECK_FALSE(c.available());
    CHECK(c.registerMember("x", 0, 0) == SEAM_HUB_NO_HANDLE);

    // The stub's reads return 7; a client that fell through the gate would
    // report things that do not exist.
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    CHECK(c.members(out, SEAM_HUB_MAX_MEMBERS) == 0);
    SeamHubProposal p{};
    CHECK(c.proposals(0, &p, 1) == 0);
    CHECK(c.lifecycle(0, 1) == kSeamHubNone);
}
