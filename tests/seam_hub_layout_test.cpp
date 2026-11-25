#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub.h"

#include <cstddef>

TEST_CASE("the ABI layouts are pinned") {
    CHECK(sizeof(SeamHubDecoder) == 56);
    CHECK(sizeof(SeamHubState) == 88);
    CHECK(sizeof(SeamHubBand) == 32);
    CHECK(sizeof(SeamHubBandTable) == 1216);
    CHECK(sizeof(SeamHubSection) == 32);
    CHECK(sizeof(SeamHubSections) == 600);
    CHECK(sizeof(SeamHubProposal) == 4144);
    CHECK(sizeof(SeamHubMember) == 128);
    CHECK(sizeof(SeamHubSheetStatus) == 528);
}

TEST_CASE("the offsets a reader reaches first are pinned") {
    CHECK(offsetof(SeamHubDecoder, cutHz) == 16);
    CHECK(offsetof(SeamHubState, u) == 32);
    CHECK(offsetof(SeamHubBandTable, sampleRate) == 24);
    CHECK(offsetof(SeamHubBandTable, emitterKind) == 32);
    CHECK(offsetof(SeamHubBandTable, emitterSlots) == 36);
    CHECK(offsetof(SeamHubBandTable, decoder) == 40);
    CHECK(offsetof(SeamHubProposal, authorName) == 32);
    CHECK(offsetof(SeamHubProposal, u) == 48);
    CHECK(offsetof(SeamHubMember, epoch) == 36);
    CHECK(offsetof(SeamHubMember, state) == 40);
}

TEST_CASE("the payload reserve covers every arm") {
    SeamHubProposal p;
    CHECK(sizeof(p.u) == SEAM_HUB_PAYLOAD);
    CHECK(sizeof(p.u.table) <= SEAM_HUB_PAYLOAD);
    CHECK(sizeof(p.u.sections) <= SEAM_HUB_PAYLOAD);
}

TEST_CASE("version is 1") { CHECK(seam_hub_v1_version() == 1u); }

TEST_CASE("the type bits fit a 32-bit mask") { CHECK(kSeamHubSections < 32); }

TEST_CASE("a handle's member and epoch round-trip what members() reports") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t h = seam_hub_v1_register(hub, "layout", 0, 0, 0);
    REQUIRE(h != SEAM_HUB_NO_HANDLE);
    SeamHubMember out[SEAM_HUB_MAX_MEMBERS];
    REQUIRE(seam_hub_v1_members(hub, out, SEAM_HUB_MAX_MEMBERS) == 1);
    CHECK(out[0].member == seam_hub_handle_member(h));
    CHECK(out[0].epoch == seam_hub_handle_epoch(h));
    seam_hub_v1_unregister(hub, h);
}
