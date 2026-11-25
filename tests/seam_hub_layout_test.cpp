#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub.h"

TEST_CASE("the ABI layouts are pinned") {
    CHECK(sizeof(SeamHubState) == 88);
    CHECK(sizeof(SeamHubBandTable) == 1200);
    CHECK(sizeof(SeamHubSections) == 600);
    CHECK(sizeof(SeamHubProposal) == 1232);
    CHECK(sizeof(SeamHubMember) == 128);
    CHECK(sizeof(SeamHubSheetStatus) == 528);
}

TEST_CASE("version is 1") { CHECK(seam_hub_v1_version() == 1u); }

TEST_CASE("the type bits fit a 32-bit mask") { CHECK(kSeamHubSections < 32); }
