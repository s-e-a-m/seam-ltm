#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_delays.h"
#include <vector>

using Seam::IntegerDelay;

// de.delay(maxdel, d) for an integer d: y[n] = x[n-d], zero before the start.
static std::vector<double> ramp(int n) {
    std::vector<double> x((size_t)n);
    for (int i = 0; i < n; ++i) x[(size_t)i] = 1.0 + i;   // never 0: a wrong index shows
    return x;
}

static void checkDelay(uint32_t d, std::size_t len) {
    std::vector<double> buf(len, 0.0);
    IntegerDelay dl;
    dl.attach(buf.data(), len);
    dl.setDelay(d);
    const auto x = ramp(200);
    for (int i = 0; i < 200; ++i) {
        const double want = i >= (int)d ? x[(size_t)(i - (int)d)] : 0.0;
        CAPTURE(d); CAPTURE(i);
        REQUIRE(dl.tick(x[(size_t)i]) == want);
    }
}

TEST_CASE("IntegerDelay is de.delay for d = 0, 1, 2 and the longest the buffer holds") {
    checkDelay(0, 8);      // d = 0 returns x itself
    checkDelay(1, 8);
    checkDelay(2, 8);
    checkDelay(7, 8);      // len - 1: the maximum
    checkDelay(37, 64);
}

TEST_CASE("a new delay is heard on the next tick and reads the history") {
    std::vector<double> buf(16, 0.0);
    IntegerDelay dl;
    dl.attach(buf.data(), 16);
    dl.setDelay(3);
    const auto x = ramp(40);
    for (int i = 0; i < 20; ++i) dl.tick(x[(size_t)i]);
    dl.setDelay(5);                                 // a jump, as the spec's de.delay
    CHECK(dl.tick(x[20]) == x[15]);
    dl.setDelay(1);
    CHECK(dl.tick(x[21]) == x[20]);
}

TEST_CASE("clear() empties the history") {
    std::vector<double> buf(8, 0.0);
    IntegerDelay dl;
    dl.attach(buf.data(), 8);
    dl.setDelay(4);
    for (int i = 0; i < 10; ++i) dl.tick(1.0);
    dl.clear();
    for (int i = 0; i < 4; ++i) CHECK(dl.tick(0.5) == 0.0);
    CHECK(dl.tick(0.5) == 0.5);
}

TEST_CASE("a delay longer than the buffer is clamped to len - 1, never read out of bounds") {
    std::vector<double> buf(8, 0.0);
    IntegerDelay dl;
    dl.attach(buf.data(), 8);
    dl.setDelay(100);
    CHECK(dl.delay() == 7);
}
