// The fold: a host's leave-and-return in one instant becomes one resumes line.
// Pure, links nothing but doctest. event() prints LOCAL time, so TZ is pinned.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_hub_fold.h"

#include <cstdlib>

using namespace Seam::hubfold;

namespace {
struct Tz { Tz() { setenv("TZ", "UTC", 1); tzset(); } } g_tz;

Entry mk(Kind k, const char* who, int64_t ms, const char* masks = "says -  hears pink") {
    Entry e;
    e.kind = k; e.who = who; e.unixMs = ms; e.masks = masks;
    e.text = Seam::hubsheet::event(ms, who, k == Kind::Leaves ? "leaves" : "arrives", masks);
    return e;
}
Entry other(int64_t ms) {
    Entry e;
    e.unixMs = ms;
    e.text = Seam::hubsheet::event(ms, "hub", "sheet", "note");
    return e;
}
bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
}

TEST_CASE("a toggle in the same millisecond is one resumes line") {
    std::deque<Entry> q{mk(Kind::Leaves, "strx#1", 1000), mk(Kind::Arrives, "strx#1", 1000)};
    CHECK(fold(q, 1000, kFoldMs) == 1);
    REQUIRE(q.size() == 1);
    CHECK(has(q[0].text, "strx#1"));
    CHECK(has(q[0].text, "resumes"));
    CHECK(has(q[0].text, "after 0 ms"));
}

TEST_CASE("lines between the pair stay in their place") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0), other(10), mk(Kind::Arrives, "a#1", 20)};
    CHECK(fold(q, 20, kFoldMs) == 2);
    REQUIRE(q.size() == 2);
    CHECK(has(q[0].text, "sheet"));
    CHECK(has(q[1].text, "resumes"));
    CHECK(has(q[1].text, "after 20 ms"));
}

TEST_CASE("a young leave waits, and what follows it waits too") {
    std::deque<Entry> q{other(0), mk(Kind::Leaves, "a#1", 500), other(600)};
    CHECK(fold(q, 900, kFoldMs) == 1);
    CHECK(q.size() == 3);
}

TEST_CASE("an old leave is ready") {
    std::deque<Entry> q{other(0), mk(Kind::Leaves, "a#1", 500), other(600)};
    CHECK(fold(q, 1600, kFoldMs) == 3);
    CHECK(q.size() == 3);
}

TEST_CASE("a return after the window is not folded") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0), mk(Kind::Arrives, "a#1", 1500)};
    CHECK(fold(q, 1600, kFoldMs) == 2);
    CHECK(q.size() == 2);
}

TEST_CASE("two instances are two members") {
    std::deque<Entry> q{mk(Kind::Leaves, "strx#1", 0), mk(Kind::Arrives, "strx#2", 0)};
    CHECK(fold(q, 0, kFoldMs) == 0);   // the leave of strx#1 is young, and nobody pairs it
    CHECK(q.size() == 2);
}

TEST_CASE("a change of masks is kept") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0, "says -  hears pink"),
                        mk(Kind::Arrives, "a#1", 0, "says pink  hears -")};
    CHECK(fold(q, 0, kFoldMs) == 0);   // the leave stays, young
    CHECK(q.size() == 2);
}

TEST_CASE("window zero writes everything") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0)};
    CHECK(fold(q, 0, 0) == 1);
}

TEST_CASE("window zero still folds the pairs that are present") {
    // A close or a quit folds with window 0: a toggle already in the queue
    // is still one `resumes`, as it would be on a tick.
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0), mk(Kind::Arrives, "a#1", 5)};
    CHECK(fold(q, 5, 0) == 1);
    REQUIRE(q.size() == 1);
    CHECK(has(q[0].text, "resumes"));
    CHECK(has(q[0].text, "after 5 ms"));
}

TEST_CASE("window zero does not fold a return after kFoldMs") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0), mk(Kind::Arrives, "a#1", kFoldMs + 1)};
    CHECK(fold(q, kFoldMs + 1, 0) == 2);
    CHECK(q.size() == 2);
}

TEST_CASE("window zero writes everything even if the clock went back") {
    // The system clock stepped back: the leave reads as written in the future.
    // A close or a quit (window 0) must still write it.
    std::deque<Entry> q{other(0), mk(Kind::Leaves, "a#1", 5000), other(5001)};
    CHECK(fold(q, 1000, 0) == 3);
    CHECK(q.size() == 3);
}

TEST_CASE("a leave in the future is ready, whatever the window") {
    // The clock stepped back after the leave was enqueued: holding it until
    // the clock catches up would hold every line behind it.
    std::deque<Entry> q{other(0), mk(Kind::Leaves, "a#1", 5000), other(5001)};
    CHECK(fold(q, 1000, kFoldMs) == q.size());
    CHECK(q.size() == 3);
}

TEST_CASE("a leave followed by a leave of the same who is not folded") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0), mk(Kind::Leaves, "a#1", 5),
                        mk(Kind::Arrives, "a#1", 10)};
    // The first leave's next event of a#1 is a leave, so it has no partner;
    // the second pairs with the arrival.
    CHECK(fold(q, 2000, kFoldMs) == 2);
    REQUIRE(q.size() == 2);
    CHECK(q[0].kind == Kind::Leaves);
    CHECK(has(q[0].text, "leaves"));
    CHECK(has(q[1].text, "resumes"));
    CHECK(has(q[1].text, "after 5 ms"));
}

TEST_CASE("chains fold pairwise") {
    std::deque<Entry> q{mk(Kind::Leaves, "a#1", 0), mk(Kind::Arrives, "a#1", 0),
                        mk(Kind::Leaves, "a#1", 5), mk(Kind::Arrives, "a#1", 5)};
    CHECK(fold(q, 5, kFoldMs) == 2);
    REQUIRE(q.size() == 2);
    CHECK(has(q[0].text, "resumes"));
    CHECK(has(q[1].text, "resumes"));
}
