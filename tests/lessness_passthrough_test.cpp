// lessness is a detector: whatever it is inserted on must come out of it
// bit for bit. These tests hold the copy (lessness_processor.h's
// passThrough) and the bus rule (equal channel counts, 1 to 64) to that.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "lessness_processor.h"

#include <cstdint>
#include <random>
#include <vector>

using Seam::lessness::passThrough;
using Seam::lessness::acceptsChannels;

namespace {

template <typename T>
std::vector<std::vector<T>> noise(int32_t channels, int32_t frames, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<T> d(T(-1), T(1));
    std::vector<std::vector<T>> v((size_t)channels, std::vector<T>((size_t)frames));
    for (auto& ch : v)
        for (auto& x : ch) x = d(rng);
    return v;
}

template <typename T>
std::vector<T*> pointers(std::vector<std::vector<T>>& v) {
    std::vector<T*> p;
    for (auto& ch : v) p.push_back(ch.data());
    return p;
}

} // namespace

TEST_CASE("output equals input on 1, 2, 4 and 8 channels") {
    const int32_t frames = 512;
    for (int32_t channels : {1, 2, 4, 8}) {
        CAPTURE(channels);
        auto in = noise<float>(channels, frames, 7u + (uint32_t)channels);
        const auto before = in;
        std::vector<std::vector<float>> out((size_t)channels,
                                            std::vector<float>((size_t)frames, 0.f));
        auto pi = pointers(in);
        auto po = pointers(out);
        passThrough(pi.data(), po.data(), channels, frames);
        CHECK(out == in);       // bitwise: operator== on float vectors, no tolerance
        CHECK(in == before);    // the input is read, never written
    }
}

TEST_CASE("in place is a no-op") {
    const int32_t channels = 4, frames = 256;
    auto buf = noise<float>(channels, frames, 11u);
    const auto before = buf;
    auto p = pointers(buf);
    passThrough(p.data(), p.data(), channels, frames);   // out == in, pointer for pointer
    CHECK(buf == before);
}

TEST_CASE("the double overload copies too") {
    const int32_t channels = 2, frames = 300;
    auto in = noise<double>(channels, frames, 13u);
    std::vector<std::vector<double>> out((size_t)channels,
                                         std::vector<double>((size_t)frames, 0.0));
    auto pi = pointers(in);
    auto po = pointers(out);
    passThrough(pi.data(), po.data(), channels, frames);
    CHECK(out == in);
}

TEST_CASE("equal channel counts from 1 to 64 are accepted, anything else refused") {
    for (int32_t n : {1, 2, 4, 8, 64}) {
        CAPTURE(n);
        CHECK(acceptsChannels(n, n));
    }
    CHECK_FALSE(acceptsChannels(2, 1));
    CHECK_FALSE(acceptsChannels(1, 2));
    CHECK_FALSE(acceptsChannels(4, 8));
    CHECK_FALSE(acceptsChannels(0, 0));
    CHECK_FALSE(acceptsChannels(65, 65));
}
