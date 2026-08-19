// tools/bench-pink.cpp — how much does the pinking filter cost?
//
// Two loop orders are timed, at two sample rates, at whatever
// kPolesPerOctave the included multipink_pink.h currently has (recompile
// with a different value in the header to compare densities).
//
//   Order A — section-outer, channel-middle, sample-inner.
//     This is the production code itself, Seam::multipink::pinkFilterBlock
//     in multipink_pink.h, which is what the processor calls: one
//     coefficient triple loaded once per section, but ~16-18 full passes
//     over the whole 128-256 KB scratch buffer per block, which does not
//     fit L1/L2 and is cache-pessimistic.
//
//   Order B — channel-outer, sample-middle, section-inner (interchanged).
//     One channel's row (2-4 KB) stays resident in L1 for the whole block,
//     and only <=18 state scalars are carried per channel across the
//     section loop. Coefficients (up to 32 sections x 3 doubles) are
//     re-read every sample, but that table is tiny and cache-resident too.
//
// This tool MEASURES. It does not change the production loop order — that
// stays section-outer per multipink_processor.cpp; a switch to order B is a
// candidate for a later, separate piece of work.
//
// Build: clang++ -O3 -std=c++17 -I../plugins/multipink/source -o bench-pink bench-pink.cpp
#include "multipink_pink.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr int kPool = 64, kBlock = 512, kBlocks = 4000;
constexpr int kRepeats = 7;   // repetitions per (order, fs) cell, for spread

// Order A — the production loop order: section-outer, channel-middle,
// sample-inner. Not a mirror of the production loop any more but the very
// same code, Seam::multipink::pinkFilterBlock, which is what the processor
// calls. Timings re-measured after the switch and unchanged within the
// run-to-run spread, so the recorded numbers still mean what they meant.
double runOrderA(const Seam::multipink::PinkDesign& d,
                  std::vector<float>& scratch, std::vector<float>& state) {
    using namespace std::chrono;
    std::fill(scratch.begin(), scratch.end(), 0.1f);
    std::fill(state.begin(), state.end(), 0.0f);
    const auto t0 = steady_clock::now();
    for (int b = 0; b < kBlocks; ++b)
        Seam::multipink::pinkFilterBlock<float>(d, scratch.data(), kPool, kBlock,
                                                state.data(), kPool);
    return duration<double>(steady_clock::now() - t0).count();
}

// Order B — interchanged: channel-outer, sample-middle, section-inner.
// State is laid out per-channel (kMaxSections scalars, contiguous) so the
// whole per-channel state fits in a handful of cache lines and the row
// (kBlock floats) is read and written exactly once.
double runOrderB(const Seam::multipink::PinkDesign& d,
                  std::vector<float>& scratch, std::vector<float>& stateByCh) {
    using namespace std::chrono;
    std::fill(scratch.begin(), scratch.end(), 0.1f);
    std::fill(stateByCh.begin(), stateByCh.end(), 0.0f);
    const int ns = d.numSections;
    float b0[Seam::multipink::PinkDesign::kMaxSections];
    float b1[Seam::multipink::PinkDesign::kMaxSections];
    float a1[Seam::multipink::PinkDesign::kMaxSections];
    for (int i = 0; i < ns; ++i) { b0[i] = (float)d.b0[i]; b1[i] = (float)d.b1[i]; a1[i] = (float)d.a1[i]; }
    const auto t0 = steady_clock::now();
    for (int b = 0; b < kBlocks; ++b)
        for (int ch = 0; ch < kPool; ++ch) {
            float* row = scratch.data() + (size_t)ch * kBlock;
            float* s = stateByCh.data() + (size_t)ch * Seam::multipink::PinkDesign::kMaxSections;
            for (int n = 0; n < kBlock; ++n) {
                float x = row[n];
                for (int sec = 0; sec < ns; ++sec) {
                    const float y = b0[sec] * x + s[sec];
                    s[sec] = b1[sec] * x - a1[sec] * y;
                    x = y;
                }
                row[n] = x;
            }
        }
    return duration<double>(steady_clock::now() - t0).count();
}

struct Stats { double min, max, mean; };

Stats stats(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    double sum = 0.0;
    for (double x : v) sum += x;
    return { v.front(), v.back(), sum / (double)v.size() };
}

} // namespace

int main() {
    std::printf("kPolesPerOctave = %.3f   kPool=%d kBlock=%d kBlocks=%d, %d repeats/cell\n\n",
                Seam::multipink::PinkDesign::kPolesPerOctave, kPool, kBlock, kBlocks, kRepeats);
    std::printf("%-6s %-10s %3s %9s %9s %9s %9s   %s\n",
                "fs", "order", "sec", "min(s)", "mean(s)", "max(s)", "%core(mean)", "audio(s)");

    for (double fs : {48000.0, 192000.0}) {
        Seam::multipink::PinkDesign d;
        d.design(fs);
        std::vector<float> scratch((size_t)kPool * kBlock, 0.1f);
        std::vector<float> stateA((size_t)d.numSections * kPool, 0.0f);
        std::vector<float> stateB((size_t)kPool * Seam::multipink::PinkDesign::kMaxSections, 0.0f);
        const double audioSeconds = (double)kBlocks * kBlock / fs;

        std::vector<double> timesA, timesB;
        for (int r = 0; r < kRepeats; ++r) {
            timesA.push_back(runOrderA(d, scratch, stateA));
            timesB.push_back(runOrderB(d, scratch, stateB));
        }
        Stats sa = stats(timesA), sb = stats(timesB);
        std::printf("%-6.0f %-10s %3d %9.4f %9.4f %9.4f %10.3f%%   %.1f\n",
                    fs, "A(prod)", d.numSections, sa.min, sa.mean, sa.max,
                    100.0 * sa.mean / audioSeconds, audioSeconds);
        std::printf("%-6.0f %-10s %3d %9.4f %9.4f %9.4f %10.3f%%   %.1f\n",
                    fs, "B(interch)", d.numSections, sb.min, sb.mean, sb.max,
                    100.0 * sb.mean / audioSeconds, audioSeconds);
    }
    return 0;
}
