#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub.h"

#include <cstring>
#include <string>
#include <vector>

extern "C" int32_t seam_hub_test_recent_lines(char* out, int32_t cap);

namespace {

const uint32_t kTable = SEAM_HUB_BIT(kSeamHubBandTable);

std::vector<std::string> recentLines() {
    static char buf[256 * 200];
    const int32_t n = seam_hub_test_recent_lines(buf, sizeof buf);
    std::vector<std::string> v;
    std::string all(buf);
    size_t p = 0;
    while (p < all.size()) { size_t e = all.find('\n', p); v.push_back(all.substr(p, e - p)); p = e + 1; }
    CHECK((int32_t)v.size() == n);
    return v;
}

SeamHubProposal table(uint32_t stone) {
    SeamHubProposal p;
    std::memset(&p, 0, sizeof p);
    p.type = kSeamHubBandTable;
    p.stoneId = stone;
    p.u.table.bandCount = SEAM_HUB_BANDS;
    return p;
}

int32_t member(const char* name, uint32_t says, uint32_t hears, uint32_t stone, bool withState = true) {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t h = seam_hub_v1_register(hub, name, says, hears);
    if (withState) {
        SeamHubState st{};
        st.kind = kSeamHubPink;
        st.stoneId = stone;
        seam_hub_v1_publish_state(hub, h, &st);
    }
    return h;
}

} // namespace

TEST_CASE("ids are monotone and assigned by post") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("author", kTable, 0, 1);
    SeamHubProposal p = table(1);
    p.id = 999; p.authorMember = 77; p.authorInstance = 77;   // must be overwritten
    const uint64_t i1 = seam_hub_v1_post(hub, a, &p);
    CHECK(i1 != 0);
    CHECK(p.id == i1);
    CHECK(p.authorInstance == 1u);
    CHECK(p.postedUnixMs > 1700000000000ll);
    SeamHubProposal q = table(1);
    const uint64_t i2 = seam_hub_v1_post(hub, a, &q);
    CHECK(i2 == i1 + 1);
    SeamHubProposal out[4];
    CHECK(seam_hub_v1_proposals(hub, i1 - 1, out, 4) == 2);
    CHECK(out[0].id == i1);
    CHECK(out[1].id == i2);
    CHECK(out[0].authorMember == p.authorMember);
    seam_hub_v1_unregister(hub, a);
}

TEST_CASE("post refuses a type the author does not say") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("mute", SEAM_HUB_BIT(kSeamHubSections), 0, 1);
    SeamHubProposal p = table(1);
    CHECK(seam_hub_v1_post(hub, a, &p) == 0);
    CHECK(seam_hub_v1_post(hub, SEAM_HUB_NO_HANDLE, &p) == 0);
    CHECK(seam_hub_v1_post(nullptr, a, &p) == 0);
    CHECK(seam_hub_v1_post(hub, a, nullptr) == 0);
    p.type = 40;   // outside the mask width
    CHECK(seam_hub_v1_post(hub, a, &p) == 0);
    seam_hub_v1_unregister(hub, a);
    SeamHubProposal q = table(1);
    CHECK(seam_hub_v1_post(hub, a, &q) == 0);   // stale handle
}

TEST_CASE("stone routing") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a  = member("author", kTable, 0, 1);
    const int32_t r1 = member("r-one", 0, kTable, 1);
    const int32_t r0 = member("r-none", 0, kTable, 0, false);   // no state: stone 0
    const int32_t r2 = member("r-two", 0, kTable, 2);
    const int32_t rx = member("r-deaf", 0, 0, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(seam_hub_v1_get(), a, &p);
    REQUIRE(id != 0);
    CHECK(seam_hub_v1_lifecycle(hub, r1, id) == kSeamHubArrived);
    CHECK(seam_hub_v1_lifecycle(hub, r0, id) == kSeamHubArrived);
    CHECK(seam_hub_v1_lifecycle(hub, r2, id) == kSeamHubNone);
    CHECK(seam_hub_v1_lifecycle(hub, rx, id) == kSeamHubNone);
    CHECK(seam_hub_v1_lifecycle(hub, SEAM_HUB_NO_HANDLE, id) == kSeamHubNone);
    // A proposal for stone 0 reaches everyone who hears the type.
    SeamHubProposal z = table(0);
    const uint64_t zid = seam_hub_v1_post(hub, a, &z);
    CHECK(seam_hub_v1_lifecycle(hub, r2, zid) == kSeamHubArrived);
    CHECK(seam_hub_v1_transition(hub, r2, id, kSeamHubShown, 0) == -2);
    for (int32_t h : {a, r1, r0, r2, rx}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("the lifecycle is per receiver") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("author", kTable, 0, 1);
    const int32_t r = member("recv", 0, kTable, 1);
    const int32_t s = member("recv", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0) == 0);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubApplied, 1) == 0);
    CHECK(seam_hub_v1_lifecycle(hub, r, id) == kSeamHubApplied);
    CHECK(seam_hub_v1_lifecycle(hub, s, id) == kSeamHubArrived);
    CHECK(seam_hub_v1_transition(hub, 12345678, id, kSeamHubShown, 0) == -2);   // unknown receiver
    CHECK(seam_hub_v1_transition(hub, r, id + 1000, kSeamHubShown, 0) == -1);
    for (int32_t h : {a, r, s}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("a new member in a reused slot does not inherit the lifecycle") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("author", kTable, 0, 1);
    int32_t r = member("recv", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    REQUIRE(seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0) == 0);
    seam_hub_v1_unregister(hub, r);
    const int32_t r2 = member("recv", 0, kTable, 1);
    CHECK(seam_hub_v1_lifecycle(hub, r2, id) == kSeamHubArrived);
    CHECK(seam_hub_v1_lifecycle(hub, r, id) == kSeamHubNone);   // the old handle is dead
    seam_hub_v1_unregister(hub, r2);
    seam_hub_v1_unregister(hub, a);
}

TEST_CASE("applying needs the operator") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("author", kTable, 0, 1);
    const int32_t r = member("recv", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubApplied, 0) == -4);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubIgnored, 0) == -4);
    CHECK(seam_hub_v1_lifecycle(hub, r, id) == kSeamHubShown);
    for (int32_t h : {a, r}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("arrived cannot jump to applied") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("author", kTable, 0, 1);
    const int32_t r = member("recv", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubApplied, 1) == -3);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubIgnored, 1) == -3);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0) == 0);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0) == -3);     // not a loop
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubArrived, 0) == -3);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubNone, 0) == -3);
    for (int32_t h : {a, r}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("a proposal can be applied again") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("bastone", kTable, 0, 1);
    const int32_t r = member("bastone", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    REQUIRE(seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0) == 0);
    const size_t before = recentLines().size();
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubApplied, 1) == 0);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubIgnored, 1) == 0);
    CHECK(seam_hub_v1_transition(hub, r, id, kSeamHubApplied, 1) == 0);
    const auto lines = recentLines();
    REQUIRE(lines.size() == before + 3);
    const std::string idTxt = "#" + std::to_string(id);
    const std::string& last = lines[lines.size() - 1];
    CHECK(last.find("operator  applies  band-table " + idTxt + "  on bastone#2") != std::string::npos);
    CHECK(lines[lines.size() - 2].find("operator  ignores  band-table " + idTxt + "  on bastone#2") != std::string::npos);
    for (int32_t h : {a, r}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("post and shows are enqueued with who and the type") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("bastone", kTable, 0, 1);
    const int32_t r = member("strx", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t id = seam_hub_v1_post(hub, a, &p);
    seam_hub_v1_transition(hub, r, id, kSeamHubShown, 0);
    const auto lines = recentLines();
    const std::string idTxt = "#" + std::to_string(id);
    CHECK(lines[lines.size() - 2].find("bastone#1  says  band-table " + idTxt) != std::string::npos);
    CHECK(lines[lines.size() - 1].find("strx#1  shows  band-table " + idTxt) != std::string::npos);
    // A refused call leaves no line.
    const size_t n = lines.size();
    seam_hub_v1_transition(hub, r, id, kSeamHubApplied, 0);
    CHECK(recentLines().size() == n);
    for (int32_t h : {a, r}) seam_hub_v1_unregister(hub, h);
}

TEST_CASE("the store keeps the last 64") {
    SeamHub* hub = seam_hub_v1_get();
    const int32_t a = member("author", kTable, 0, 1);
    const int32_t r = member("recv", 0, kTable, 1);
    SeamHubProposal p = table(1);
    const uint64_t first = seam_hub_v1_post(hub, a, &p);
    for (int i = 1; i < 70; ++i) { SeamHubProposal q = table(1); seam_hub_v1_post(hub, a, &q); }
    static SeamHubProposal out[100];
    REQUIRE(seam_hub_v1_proposals(hub, 0, out, 100) == SEAM_HUB_STORE);
    CHECK(out[0].id == first + 6);
    CHECK(out[SEAM_HUB_STORE - 1].id == first + 69);
    for (int i = 1; i < SEAM_HUB_STORE; ++i) CHECK(out[i].id == out[i - 1].id + 1);
    CHECK(seam_hub_v1_lifecycle(hub, r, first) == kSeamHubNone);
    CHECK(seam_hub_v1_transition(hub, r, first, kSeamHubShown, 0) == -1);
    CHECK(seam_hub_v1_lifecycle(hub, r, first + 6) == kSeamHubArrived);
    CHECK(seam_hub_v1_proposals(hub, first + 66, out, 100) == 3);   // afterId filters
    CHECK(seam_hub_v1_proposals(hub, 0, out, 5) == 5);              // max limits, oldest first
    CHECK(out[0].id == first + 6);
    // An evicted slot's lifecycle does not leak into the proposal that replaced it.
    CHECK(seam_hub_v1_transition(hub, r, first + 6, kSeamHubShown, 0) == 0);
    for (int i = 0; i < SEAM_HUB_STORE; ++i) { SeamHubProposal q = table(1); seam_hub_v1_post(hub, a, &q); }
    CHECK(seam_hub_v1_lifecycle(hub, r, first + 6) == kSeamHubNone);
    CHECK(seam_hub_v1_lifecycle(hub, r, first + 70 + SEAM_HUB_STORE - 1) == kSeamHubArrived);
    for (int32_t h : {a, r}) seam_hub_v1_unregister(hub, h);
}
