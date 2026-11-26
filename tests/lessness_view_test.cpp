#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lessness_view.h"

#include <cmath>
#include <cstring>
#include <ctime>

using namespace Seam::lessness;

static SeamHubMember mem(const char* name, uint32_t inst, uint32_t says, uint32_t hears) {
    SeamHubMember m{};
    std::strncpy(m.name, name, sizeof m.name - 1);
    m.member = inst; m.epoch = 1; m.instance = inst; m.says = says; m.hears = hears;
    return m;
}

static SeamHubProposal prop(uint64_t id) {
    SeamHubProposal p{};
    p.id = id; p.type = kSeamHubBandTable; p.stoneId = 1; p.authorInstance = 1;
    std::strncpy(p.authorName, "strx", sizeof p.authorName - 1);
    return p;
}

TEST_CASE("a member line names who, stone and masks") {
    SeamHubMember m = mem("strx", 1, 0, SEAM_HUB_BIT(kSeamHubPink) | SEAM_HUB_BIT(kSeamHubGlide) | SEAM_HUB_BIT(kSeamHubDecoder));
    m.hasState = 1; m.state.kind = kSeamHubPink; m.state.stoneId = 1;
    CHECK(memberLine(m) == "strx#1      stone 1  says -  hears pink,glide,decoder");
}

TEST_CASE("no state reads as no state") {
    SeamHubMember m = mem("strx", 1, 0, 0);
    CHECK(stateLine(m) == "no state");
    CHECK(memberLine(m) == "strx#1      stone -  says -  hears -");
    m.hasState = 1; m.state.kind = kSeamHubPink;
    CHECK(stateLine(m) == Seam::hubsheet::stateObject(m.state));
}

TEST_CASE("a pending proposal comes first") {
    SeamHubProposal p[2] = {prop(1), prop(2)};
    SeamHubMember m[1] = {mem("bastone", 1, 0, 0)};
    LifecycleOf of = [](uint32_t, uint32_t, uint64_t id) { return id == 1 ? (int32_t)kSeamHubApplied : (int32_t)kSeamHubArrived; };
    auto rows = proposalRows(p, 2, m, 1, of);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].id == 2); CHECK(rows[0].pending);
    CHECK(rows[1].id == 1); CHECK_FALSE(rows[1].pending);
}

TEST_CASE("each receiver keeps its own lifecycle") {
    SeamHubProposal p[1] = {prop(5)};
    SeamHubMember m[2] = {mem("bastone", 1, 0, 0), mem("bastooge", 1, 0, 0)};
    m[0].member = 0; m[1].member = 1; m[1].epoch = 2;
    LifecycleOf of = [](uint32_t mm, uint32_t, uint64_t) { return mm == 0 ? (int32_t)kSeamHubShown : (int32_t)kSeamHubApplied; };
    auto rows = proposalRows(p, 1, m, 2, of);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].pending);
    CHECK(receiversLine(rows[0]) == "bastone#1 shown  bastooge#1 applied");
}

TEST_CASE("a proposal nobody hears says so") {
    SeamHubProposal p[1] = {prop(5)};
    SeamHubMember m[1] = {mem("bastone", 1, 0, 0)};
    LifecycleOf of = [](uint32_t, uint32_t, uint64_t) { return (int32_t)kSeamHubNone; };
    auto rows = proposalRows(p, 1, m, 1, of);
    REQUIRE(rows.size() == 1);
    CHECK_FALSE(rows[0].pending);
    CHECK(receiversLine(rows[0]) == "heard by nobody now");
    CHECK(std::strlen(kDecisionsNote) > 0);
}

TEST_CASE("the head carries mode, time and integration") {
    SeamHubProposal p = prop(12);
    p.u.table.hasDecoder = 1; p.u.table.decoder.test = 1; p.u.table.integrationSec = 646.6;
    p.postedUnixMs = 1000LL * 1700000000;
    std::time_t t = 1700000000; std::tm tm{}; localtime_r(&t, &tm);
    char hm[8]; std::snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
    SeamHubMember m[1] = {mem("bastone", 1, 0, 0)};
    LifecycleOf of = [](uint32_t, uint32_t, uint64_t) { return (int32_t)kSeamHubArrived; };
    auto rows = proposalRows(&p, 1, m, 1, of);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].head.find("band-table #12  strx#1  stone 1  W  ") == 0);
    CHECK(rows[0].head.find(std::string("  ") + hm + "  ") != std::string::npos);
    CHECK(rows[0].head.find("647 s") != std::string::npos);
}

TEST_CASE("a sections head names block and count") {
    SeamHubProposal p = prop(3);
    p.type = kSeamHubSections; p.u.sections.block = kSeamHubBlockDifference; p.u.sections.count = 6;
    SeamHubMember m[1] = {mem("bastone", 1, 0, 0)};
    LifecycleOf of = [](uint32_t, uint32_t, uint64_t) { return (int32_t)kSeamHubShown; };
    auto rows = proposalRows(&p, 1, m, 1, of);
    CHECK(rows[0].head.find("sections #3  strx#1  stone 1  difference") == 0);
    CHECK(rows[0].head.find("6 sections") != std::string::npos);
}

TEST_CASE("the sheet line and its alarm") {
    SeamHubSheetStatus s{};
    CHECK(sheetLine(s, false) == "hub unavailable"); CHECK(sheetAlarm(s, false));
    CHECK(sheetLine(s, true) == "no session"); CHECK_FALSE(sheetAlarm(s, true));
    std::strcpy(s.path, "/tmp/a-seam-sheet.txt"); s.writing = 1;
    std::string l = sheetLine(s, true);
    CHECK(l.size() >= 7); CHECK(l.substr(l.size() - 7) == "writing");
    CHECK_FALSE(sheetAlarm(s, true));
    s.writing = 0; s.pendingLines = 120; s.droppedLines = 3;
    l = sheetLine(s, true);
    CHECK(l.find("120 lines held") != std::string::npos);
    CHECK(l.find("3 dropped") != std::string::npos);
    CHECK(l.find("folder unreachable") != std::string::npos);
    CHECK(sheetAlarm(s, true));
}

TEST_CASE("a sheet not open with nothing held raises no alarm") {
    // Between begin() and open() on every new sheet, and while a sheet whose
    // folder failed at its first open has nothing to write: nothing at risk.
    SeamHubSheetStatus s{};
    std::strcpy(s.path, "/tmp/a-seam-sheet.txt"); s.writing = 0; s.pendingLines = 0; s.droppedLines = 2;
    const std::string l = sheetLine(s, true);
    CHECK(l.find("not open, nothing held") != std::string::npos);
    CHECK(l.find("unreachable") == std::string::npos);
    CHECK_FALSE(sheetAlarm(s, true));
    s.pendingLines = 1;   // the first held line: the alarm rises
    CHECK(sheetAlarm(s, true));
}

TEST_CASE("the decisions note names both resets and fits its line") {
    const std::string n = kDecisionsNote;
    CHECK(n.find("stone") != std::string::npos);
    CHECK(n.find("off-and-on") != std::string::npos);
    CHECK(n.find("sheet keeps them") != std::string::npos);
    CHECK((int)n.size() <= kNoteChars);
}

TEST_CASE("the footer names NEW SHEET only while a sheet is written") {
    const std::string folder = "/tmp/next";
    SeamHubSheetStatus s{};
    std::strcpy(s.path, "/tmp/a-seam-sheet.txt");
    s.writing = 1;
    CHECK(footerLine(s, true, folder) == "next sheet in /tmp/next \xC2\xB7 NEW SHEET to start it");
    CHECK(footerLine(s, true, "") == sheetLine(s, true));   // no folder waiting
    // Held after the grace: sheet_new() is ignored, the held count stays.
    s.writing = 0; s.pendingLines = 7;
    CHECK(footerLine(s, true, folder) == sheetLine(s, true));
    CHECK(footerLine(s, true, folder).find("7 lines held") != std::string::npos);
    // No session: the next session starts there by itself, no button named.
    SeamHubSheetStatus none{};
    CHECK(footerLine(none, true, folder) == "next sheet in /tmp/next");
    CHECK(footerLine(none, false, folder) == "hub unavailable");
}

TEST_CASE("the home folder reads as ~") {
    CHECK(tildePath("/Users/g/Documents/x", "/Users/g") == "~/Documents/x");
    CHECK(tildePath("/opt/x", "/Users/g") == "/opt/x");
    CHECK(tildePath("/Users/g/x", nullptr) == "/Users/g/x");
    CHECK(tildePath("/Users/gg/x", "/Users/g") == "/Users/gg/x");
}

TEST_CASE("a non-finite integration prints a dash") {
    SeamHubProposal p = prop(1);
    p.u.table.integrationSec = std::nan("");
    SeamHubMember m[1] = {mem("bastone", 1, 0, 0)};
    LifecycleOf of = [](uint32_t, uint32_t, uint64_t) { return (int32_t)kSeamHubArrived; };
    auto rows = proposalRows(&p, 1, m, 1, of);
    CHECK(rows[0].head.find("  - s") != std::string::npos);
    p.u.table.integrationSec = 1e300;
    CHECK(proposalRows(&p, 1, m, 1, of)[0].head.find("  - s") != std::string::npos);
}

TEST_CASE("a path of control characters only is no session, with no alarm") {
    SeamHubSheetStatus s{};
    s.path[0] = '\n'; s.writing = 0;
    CHECK(sheetLine(s, true) == "no session");
    CHECK_FALSE(sheetAlarm(s, true));
}
