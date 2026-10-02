# stunedrev Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A hand-written C++ VST3 of `sdt.stunedrev` (four lines of 42 Moorer all-passes, ratios √2 φ e π) for SSCDO#2, equal to the Faust spec to numerical precision, with an exactly sized arena allocated outside the audio thread and a RESET that empties it while playing.

**Architecture:** Two new reusable headers, `plugins/_common/seam_primes.h` (sieve + `sff.np` + `sma.ms2npsamp`) and `plugins/_common/seam_moorer.h` (Moorer's all-pass, the C++ side of `seam.moorer.lib`), feed an SDK-free engine `plugins/stunedrev/source/stunedrev_dsp.h` (sections in one arena, gain ramps, RESET state machine), driven by a thin `SingleComponentEffect` processor with a ParamBox of atomics and two GUI-only views (RESET, footer). Faust references are rendered once by a committed script into a committed header, so the doctest suite proves C++ == spec without `faust`.

**Tech Stack:** C++17, VST3 SDK + VSTGUI, CMake (Xcode generator), doctest, Faust 2.88 + faustlibraries clone (reference generation only), Python 3 (lint, docs), LuaLaTeX (report).

**Spec:** `docs/superpowers/specs/2026-10-01-stunedrev-plugin-design.md` (approved 2026-10-01). Read it before any task.

## Global Constraints

- State and arithmetic in `double` throughout; `float`/`double` conversion only at the bus.
- The spec is `sdt.stunedrev`, `sdt.stline`, `sdt.stdel`, `sjm.apfv`, `sma.ms2npsamp` (faust-libraries 9765bb4 or later). Do not change them.
- Delays: `n = floor(ms·(i+1)·k·fs/1000 + 0.5)`, kept when `n < 2`, else the smallest prime **strictly greater** than n. Primes are computed at the session's rate (never at 96 kHz and converted).
- g = 1/√2 in every section; ratios in line order √2, (1+√5)/2, 2.718281828459045, 3.141592653589793; starting times 83, 47, 7, 71 ms; times 1–100 ms, integer step.
- Section length `msToPrimeSamples(100·(i+1)·k, fs) + 1`; one arena; allocated and zeroed in `setActive(true)`, never in `process()`.
- RESET is not a VST3 parameter and not in the state.
- Filters are reusable C++ libraries in `plugins/_common/`, as in Faust (Giuseppe): `seam_<lib>.h` is the C++ side of `seam.<lib>.lib`, cites its Faust in the header, and knows nothing of the plugin that uses it.
- Ramps of 25 ms (`Seam::LinearRamp`) for input, output, POWER, RESET fade.
- UI: `doc/style/ui-style.md`, format L (460 px), title `SEAM STUNEDREV`, factory name `SEAM STUNEDREV`, `tools/check-uidesc.py` clean, palette names only (never `TextDim`, in the uidesc or in C++).
- FUID `0x5E4D0011, 0xA1B2C3D4, 0x53545200, 0x00000011` (word3 = ASCII "STR\0"); subcategory `Fx|Reverb`; bundle id `io.github.s-e-a-m.stunedrev`.
- Build: `cmake --build build --config Release --target stunedrev` (Xcode generator, SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`). Tests: `cmake --build build-test --config Release && ctest --test-dir build-test -C Release` (`build-test` has `SEAM_BUILD_PLUGINS=OFF`; never build plugins there, it would steal the VST3 symlinks).
- doctest `Approx` is banned for small values (suite trap): use explicit absolute or relative tolerances.
- Code, comments, commits and docs in English; one sentence per line in prose docs; affirmative voice.
- Commit as you go (seam-ltm, faust-libraries); never push and never run `make -C doc publish` without asking Giuseppe.
- Commit trailer on every commit:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM
  ```

## Review Focus

1. **In-place buffers.** A host may pass the same pointers for input and output; the output must equal the out-of-place output exactly (test in Task 4).
2. **Block sizes the suite never sees.** 1-sample, prime-sized and 4093-sample blocks must give exactly the output of 256-sample blocks, including across a RESET (tests in Tasks 4 and 5).
3. **A rate change in the session.** `prepare(48000)` after a run at 96 kHz must give the state, delays and centroids of a fresh 48 kHz engine (test in Task 4).
4. **RESET abused.** A second click during the clearing restarts it; a RESET with POWER off still clears and stays silent; a RESET before `prepare` is not replayed after it (tests in Task 5).
5. **process() without memory.** Before `prepare`, after `release`, or after an allocation failure, `process()` writes zeros and never touches the arena (test in Task 4).
6. **Host values off the step grid.** A normalized time that falls between two steps must map to the millisecond the SDK's `RangeParameter` displays (test in Task 6).

---

### Task 1: `seam_primes.h` — the sieve, `sff.np`, `sma.ms2npsamp`

**Files:**
- Create: `plugins/_common/seam_primes.h`
- Create: `tests/seam_primes_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a block)

**Interfaces:**
- Produces: `Seam::PrimeSieve(uint32_t bound)`, `bool isPrime(uint32_t) const`, `uint32_t nextPrimeAbove(uint32_t) const` (0 when no prime ≤ bound), `uint32_t bound() const`; `uint32_t Seam::msToPrimeSamples(double ms, double fs, const PrimeSieve&)`.

- [ ] **Step 0: Reconfigure the test tree at the 11.0 floor.** The `build-test` cache still holds 15.7 (a known pre-existing `minos_lint` failure).

Run: `cmake -S . -B build-test -DSEAM_BUILD_PLUGINS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk && cmake --build build-test --config Release && ctest --test-dir build-test -C Release 2>&1 | tail -3`
Expected: every test passes, `minos_lint` included. If `minos_lint` still fails, record the output in the log and go on (it predates this branch).

- [ ] **Step 1: Write the failing test** `tests/seam_primes_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_primes.h"
#include <cstdint>
#include <vector>

using Seam::PrimeSieve;

// faust-libraries src/h/nextprime.h, is_prime: the reference of sff.np.
static bool trialPrime(uint32_t n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if ((n & 1) == 0) return false;
    for (uint32_t i = 3; (uint64_t)i * i <= n; i += 2)
        if (n % i == 0) return false;
    return true;
}

// The largest n stunedrev asks at 192 kHz is round(100*42*pi*192) =
// 2 533 274; its sieve bound adds 1024.
static constexpr uint32_t kBound192 = 2533274u + 1024u;

TEST_CASE("isPrime equals trial division for every n up to the 192 kHz bound") {
    const PrimeSieve s(kBound192);
    long mismatches = 0;
    for (uint32_t n = 0; n <= kBound192; ++n)
        if (s.isPrime(n) != trialPrime(n)) ++mismatches;
    CHECK(mismatches == 0);
}

TEST_CASE("nextPrimeAbove equals nextprime.h's next_pr for every n, strictly greater") {
    const PrimeSieve s(kBound192);
    // Walk down: `above` is the smallest prime strictly greater than n.
    uint32_t above = 0;
    long mismatches = 0, notGreater = 0;
    for (uint32_t n = kBound192 + 1; n-- > 0; ) {
        const uint32_t got = s.nextPrimeAbove(n);
        if (got != above) ++mismatches;
        if (got != 0 && got <= n) ++notGreater;
        if (trialPrime(n)) above = n;
    }
    CHECK(mismatches == 0);
    CHECK(notGreater == 0);
    CHECK(s.nextPrimeAbove(0) == 2);
    CHECK(s.nextPrimeAbove(1) == 2);
    CHECK(s.nextPrimeAbove(2) == 3);
    CHECK(s.nextPrimeAbove(4) == 5);
}

TEST_CASE("msToPrimeSamples is sma.ms2npsamp: round, keep below 2, else the prime above") {
    const PrimeSieve s(100000);
    CHECK(Seam::msToPrimeSamples(0.01, 96000.0, s) == 1);        // 0.96 -> 1, kept
    CHECK(Seam::msToPrimeSamples(1.0, 96000.0, s) == 97);        // 96 -> 97
    CHECK(Seam::msToPrimeSamples(97.0 / 96.0, 96000.0, s) == 101); // 97 is prime: stepped over
    CHECK(Seam::msToPrimeSamples(1.0, 44100.0, s) == 47);        // 44.1 -> 44 -> 47
    CHECK(Seam::msToPrimeSamples(10.5 / 96.0, 96000.0, s) == 13);  // floor(10.5 + 0.5) = 11 -> 13
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# seam_primes.h: the sieve behind sff.np / sma.ms2npsamp (stunedrev, and any
# plugin with prime delays).
add_executable(seam_primes_test seam_primes_test.cpp)
target_include_directories(seam_primes_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_primes_test PRIVATE cxx_std_17)
add_test(NAME seam_primes_test COMMAND seam_primes_test)
```

- [ ] **Step 2: Run it to see it fail.** Run: `cmake -S . -B build-test && cmake --build build-test --config Release --target seam_primes_test 2>&1 | tail -3`
Expected: compile error, `seam_primes.h` not found.

- [ ] **Step 3: Write `plugins/_common/seam_primes.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_primes.h — prime delays without division on the audio thread
//
// FAUST REFERENCE:
//   sff.np         = ffunction(int next_pr(int), "../h/nextprime.h", "");
//                    the smallest prime STRICTLY greater than n
//   sma.ms2npsamp  = select2(n < 2, n : sff.np, n)
//                    with { n = int(floor(ms*ma.SR/1000 + 0.5)); };
//
// nextprime.h tests each candidate by trial division, up to ~800 divisions
// for a number near 2.5 million. A plugin that recomputes 42 delays when a
// slider moves cannot afford that inside process(), so the primes come from
// a sieve of Eratosthenes built once, outside the audio thread, over the
// odd numbers (one bit each: 317 KB at stunedrev's 192 kHz bound). A lookup
// then scans at most one prime gap (154 below 5 million).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace Seam {

class PrimeSieve {
public:
    // Allocates; call outside the audio thread.
    explicit PrimeSieve(uint32_t bound) : bound_(bound), odd_(bound / 2 + 1, true) {
        odd_[0] = false;                                     // 1 is not prime
        for (uint64_t p = 3; p * p <= bound_; p += 2)
            if (odd_[p / 2])
                for (uint64_t m = p * p; m <= bound_; m += 2 * p) odd_[m / 2] = false;
    }

    uint32_t bound() const { return bound_; }

    // Defined for n <= bound().
    bool isPrime(uint32_t n) const {
        if (n < 2) return false;
        if (n == 2) return true;
        if ((n & 1) == 0) return false;
        return odd_[n / 2];
    }

    // sff.np: the smallest prime strictly greater than n; 0 when none lies
    // within the bound (the caller sized the bound so that it never happens).
    uint32_t nextPrimeAbove(uint32_t n) const {
        if (n < 2) return 2;
        for (uint64_t c = (n & 1) ? (uint64_t)n + 2 : (uint64_t)n + 1; c <= bound_; c += 2)
            if (odd_[c / 2]) return (uint32_t)c;
        return 0;
    }

private:
    uint32_t bound_;
    std::vector<bool> odd_;   // odd_[j] stands for 2j+1
};

// sma.ms2npsamp: milliseconds to a prime number of samples at fs.
inline uint32_t msToPrimeSamples(double ms, double fs, const PrimeSieve& s) {
    const long n = (long)std::floor(ms * fs / 1000.0 + 0.5);
    if (n < 2) return n < 0 ? 0u : (uint32_t)n;
    return s.nextPrimeAbove((uint32_t)n);
}

} // namespace Seam
```

- [ ] **Step 4: Run it to see it pass.** Run: `cmake --build build-test --config Release --target seam_primes_test && ctest --test-dir build-test -C Release -R seam_primes_test --output-on-failure`
Expected: 3 test cases passed.

- [ ] **Step 5: Commit.**

```bash
git add plugins/_common/seam_primes.h tests/seam_primes_test.cpp tests/CMakeLists.txt
git commit -m "feat(common): seam_primes.h, a sieve behind sff.np and sma.ms2npsamp"
```

---

### Task 2: The Faust references

**Files:**
- Create: `tests/stunedrev_burst.h` (the test input, shared by the refdump and the tests)
- Create: `doc/study/sscdo2/stunedrev-plugin/gen-ref.sh` (executable)
- Create: `doc/study/sscdo2/stunedrev-plugin/refdump.cpp`
- Create: `doc/study/sscdo2/stunedrev-plugin/dsp/stdel.dsp`, `dsp/apfv.dsp`, `dsp/apfv07.dsp`, `dsp/stunedrev.dsp`
- Create (generated): `tests/ref/stunedrev_ref.h`, `tests/ref/seam_moorer_ref.h`

**Interfaces:**
- Produces: `struct Burst { explicit Burst(double fs); void fill(double* const* in, int n); }`, four channels, 50 ms of LCG white noise then silence.
- Produces, in namespace `moorerref` (`tests/ref/seam_moorer_ref.h`): `kApfv[512]`, `kApfv07[512]` (impulse responses of `sjm.apfv(1024, 37, g)` with g = 1/√2 and 0.7).
- Produces, in namespace `stunedrevref`: `kStdel96[100][168]`, `kStdel48[100][168]` (int; row = ms − 1, column = line·42 + section); `kWin96[4][30][512]`, `kWin96_energy[4][30]`, `kWin48[4][30][512]`, `kWin48_energy[4][30]` (sdt.stunedrev(83,47,7,71) on the burst: the first 512 samples of every second, and the energy of every second); `kChange96[4][3][512]`, `kChange96_energy[4][3]` (the same at 96 kHz, with t3 set to 9 ms at sample 48128).

- [ ] **Step 1: Write `tests/stunedrev_burst.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// stunedrev_burst.h — the input of the stunedrev references and tests
//
// 50 ms of white noise on each of the four inputs, one LCG per channel
// (seed c+1, Numerical Recipes constants), uniform on [-1, 1), then silence.
// Included by doc/study/sscdo2/stunedrev-plugin/refdump.cpp and by the
// tests, so that the Faust render and the C++ render read the same samples.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstdint>

struct Burst {
    explicit Burst(double fs) : len_(std::lround(0.05 * fs)) {
        for (int c = 0; c < 4; ++c) s_[c] = 1u + (uint32_t)c;
    }
    void fill(double* const* in, int n) {
        for (int i = 0; i < n; ++i, ++pos_)
            for (int c = 0; c < 4; ++c) {
                if (pos_ < len_) {
                    s_[c] = s_[c] * 1664525u + 1013904223u;
                    in[c][i] = (double)(s_[c] >> 8) / 8388608.0 - 1.0;
                } else {
                    in[c][i] = 0.0;
                }
            }
    }
private:
    uint32_t s_[4];
    long len_, pos_ = 0;
};
```

- [ ] **Step 2: Write the three DSP files.**

`doc/study/sscdo2/stunedrev-plugin/dsp/stdel.dsp`:
```
// The 168 delays of sdt.stdel at the time of the "ms" entry, line by line.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
ms = nentry("ms", 1, 1, 100, 1);
line(k) = par(i, 42, sdt.stdel(k, i, ms));
process = line(sqrt(2)), line((1+sqrt(5))/2), line(ma.E), line(ma.PI);
```

`dsp/apfv.dsp`:
```
// One section, Moorer's form: sjm.apfv(md, t, g) with t = 37.
import("stdfaust.lib");
sjm = library("seam.moorer.lib");
process = sjm.apfv(1024, 37, 1/sqrt(2));
```

`dsp/apfv07.dsp`:
```
// The same section with another gain: the library takes g as a parameter.
import("stdfaust.lib");
sjm = library("seam.moorer.lib");
process = sjm.apfv(1024, 37, 0.7);
```

`dsp/stunedrev.dsp`:
```
// The spec: sdt.stunedrev at the Pd patch's starting times.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
t1 = nentry("t1", 83, 1, 100, 1);
t2 = nentry("t2", 47, 1, 100, 1);
t3 = nentry("t3", 7, 1, 100, 1);
t4 = nentry("t4", 71, 1, 100, 1);
process = sdt.stunedrev(t1, t2, t3, t4);
```

- [ ] **Step 3: Write `doc/study/sscdo2/stunedrev-plugin/refdump.cpp`:**

```cpp
// refdump.cpp -- render the stunedrev specification and print it as C++
// arrays, doubles in hex-float (exact to the bit). Used by gen-ref.sh only.
//
//   refdump stdel   SR NAME                    delays for ms = 1..100 -> int NAME[100][outputs]
//   refdump impulse SR N NAME                  impulse response of output 0 -> NAME[N]
//   refdump windows SR SECONDS NAME [AT LABEL VALUE]
//       the burst (tests/stunedrev_burst.h) into the four inputs, in blocks
//       of 256; NAME[4][SECONDS][512] holds the first 512 samples of every
//       second, NAME_energy[4][SECONDS] the energy of every second. With AT,
//       the entry LABEL is set to VALUE before the block starting at sample
//       AT (a multiple of 256), as the C++ test sets the time at that block.
//
// The DSP is heap-allocated: the spec sizes its buffers at the 192 kHz
// bound of ma.SR, about 1.7 GiB.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define FAUSTFLOAT double
#include "stunedrev_burst.h"

struct Meta { virtual ~Meta() {} virtual void declare(const char*, const char*) {} };
struct UI {
    std::map<std::string, double*> zones;
    void openVerticalBox(const char*) {} void openHorizontalBox(const char*) {}
    void openTabBox(const char*) {} void closeBox() {}
    void declare(double*, const char*, const char*) {}
    void addHorizontalSlider(const char* l, double* z, double, double, double, double) { zones[l] = z; }
    void addVerticalSlider(const char* l, double* z, double, double, double, double) { zones[l] = z; }
    void addNumEntry(const char* l, double* z, double, double, double, double) { zones[l] = z; }
    void addButton(const char* l, double* z) { zones[l] = z; }
    void addCheckButton(const char* l, double* z) { zones[l] = z; }
};
struct dsp { virtual ~dsp() {} };
#include "ref.h"

static double* zone(UI& ui, const char* label) {
    auto it = ui.zones.find(label);
    if (it == ui.zones.end()) { fprintf(stderr, "no entry %s\n", label); exit(2); }
    return it->second;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "see the header of refdump.cpp\n"); return 2; }
    const std::string mode = argv[1];
    const int sr = atoi(argv[2]);
    Ref* d = new Ref;
    d->init(sr);
    UI ui; d->buildUserInterface(&ui);
    const int ni = d->getNumInputs(), no = d->getNumOutputs();

    if (mode == "stdel") {
        std::vector<double> o((size_t)no);
        std::vector<double*> op; for (auto& v : o) op.push_back(&v);
        printf("// %s: sdt.stdel at %d Hz, row = ms - 1, column = line*42 + section\n", argv[3], sr);
        printf("static const int %s[100][%d] = {\n", argv[3], no);
        for (int ms = 1; ms <= 100; ++ms) {
            *zone(ui, "ms") = ms;
            d->compute(1, nullptr, op.data());
            printf("{");
            for (int c = 0; c < no; ++c) printf("%d%s", (int)o[(size_t)c], c + 1 < no ? "," : "");
            printf("}%s\n", ms < 100 ? "," : "");
        }
        printf("};\n");
        return 0;
    }

    if (mode == "impulse") {
        const int n = atoi(argv[3]);
        std::vector<double> in((size_t)n, 0.0), out((size_t)n, 0.0);
        in[0] = 1.0;
        double* ip[1] = { in.data() }; double* op[1] = { out.data() };
        d->compute(n, ip, op);
        printf("// %s: impulse response, %d samples at %d Hz\n", argv[4], n, sr);
        printf("static const double %s[%d] = {", argv[4], n);
        for (int k = 0; k < n; ++k) printf("%a%s", out[(size_t)k], k + 1 < n ? "," : "");
        printf("};\n");
        return 0;
    }

    if (mode == "windows") {
        const int seconds = atoi(argv[3]);
        const char* name = argv[4];
        const long at = argc >= 8 ? atol(argv[5]) : -1;
        const char* label = argc >= 8 ? argv[6] : nullptr;
        const double value = argc >= 8 ? atof(argv[7]) : 0.0;
        if (ni != 4 || no != 4 || (at >= 0 && at % 256 != 0)) { fprintf(stderr, "bad shape or AT\n"); return 2; }
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* ip[4]; double* op[4];
        for (int c = 0; c < 4; ++c) { ip[c] = ib.data() + c * B; op[c] = ob.data() + c * B; }
        std::vector<double> win((size_t)4 * seconds * 512, 0.0), en((size_t)4 * seconds, 0.0);
        Burst burst((double)sr);
        const long total = (long)sr * seconds;
        for (long pos = 0; pos < total; pos += B) {
            const int m = (int)(total - pos < B ? total - pos : B);
            if (pos == at) *zone(ui, label) = value;
            burst.fill(ip, m);
            d->compute(m, ip, op);
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < m; ++k) {
                    const long g = pos + k; const int s = (int)(g / sr); const long off = g % sr;
                    const double y = op[c][k];
                    if (off < 512) win[((size_t)c * seconds + s) * 512 + off] = y;
                    en[(size_t)c * seconds + s] += y * y;
                }
        }
        printf("// %s: sdt.stunedrev on the burst at %d Hz, %d s", name, sr, seconds);
        if (at >= 0) printf(", %s = %g from sample %ld", label, value, at);
        printf("\nstatic const double %s[4][%d][512] = {\n", name, seconds);
        for (int c = 0; c < 4; ++c) {
            printf("{");
            for (int s = 0; s < seconds; ++s) {
                printf("{");
                for (int k = 0; k < 512; ++k)
                    printf("%a%s", win[((size_t)c * seconds + s) * 512 + k], k < 511 ? "," : "");
                printf("}%s", s + 1 < seconds ? "," : "");
            }
            printf("}%s\n", c < 3 ? "," : "");
        }
        printf("};\nstatic const double %s_energy[4][%d] = {\n", name, seconds);
        for (int c = 0; c < 4; ++c) {
            printf("{");
            for (int s = 0; s < seconds; ++s) printf("%a%s", en[(size_t)c * seconds + s], s + 1 < seconds ? "," : "");
            printf("}%s\n", c < 3 ? "," : "");
        }
        printf("};\n");
        return 0;
    }
    fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
```

- [ ] **Step 4: Write `doc/study/sscdo2/stunedrev-plugin/gen-ref.sh`** and `chmod +x` it:

```bash
#!/usr/bin/env bash
# gen-ref.sh -- render the Faust references of the stunedrev plugin into
# tests/ref/stunedrev_ref.h. Run by hand when the spec changes; the tests
# read the committed header and need no faust binary.
#
# FAUSTLIBS: faustlibraries clone
# SEAMLIBS:  faust-libraries/src (seam.tedesco.lib; h/nextprime.h for sff.np)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
FAUSTLIBS="${FAUSTLIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}"
SEAMLIBS="${SEAMLIBS:-$(cd "$ROOT/../faust-libraries/src" && pwd)}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
OUT="$ROOT/tests/ref/stunedrev_ref.h"
MOUT="$ROOT/tests/ref/seam_moorer_ref.h"
mkdir -p "$(dirname "$OUT")"

build() { # dsp
    faust -I "$FAUSTLIBS" -I "$SEAMLIBS" -double -lang cpp -cn Ref "$HERE/dsp/$1" -o "$WORK/ref.h"
    c++ -std=c++17 -O2 -I "$WORK" -I "$SEAMLIBS/h" -I "$ROOT/tests" "$HERE/refdump.cpp" -o "$WORK/refdump"
}

{
    echo "// GENERATED by doc/study/sscdo2/stunedrev-plugin/gen-ref.sh -- do not edit."
    echo "// $(faust --version | head -1); faustlibraries $(git -C "$FAUSTLIBS" rev-parse --short HEAD);"
    echo "// faust-libraries $(git -C "$SEAMLIBS" rev-parse --short HEAD)."
    echo "#pragma once"
    echo "namespace stunedrevref {"
    build stdel.dsp
    "$WORK/refdump" stdel 96000 kStdel96
    "$WORK/refdump" stdel 48000 kStdel48
    build stunedrev.dsp
    "$WORK/refdump" windows 96000 30 kWin96
    "$WORK/refdump" windows 48000 30 kWin48
    "$WORK/refdump" windows 96000 3 kChange96 48128 t3 9
    echo "} // namespace stunedrevref"
} > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"

{
    echo "// GENERATED by doc/study/sscdo2/stunedrev-plugin/gen-ref.sh -- do not edit."
    echo "// $(faust --version | head -1); faust-libraries $(git -C "$SEAMLIBS" rev-parse --short HEAD)."
    echo "#pragma once"
    echo "namespace moorerref {"
    build apfv.dsp
    "$WORK/refdump" impulse 96000 512 kApfv
    build apfv07.dsp
    "$WORK/refdump" impulse 96000 512 kApfv07
    echo "} // namespace moorerref"
} > "$MOUT"
echo "wrote $MOUT"
```

- [ ] **Step 5: Run it.** Run: `doc/study/sscdo2/stunedrev-plugin/gen-ref.sh`
Expected: `wrote .../tests/ref/stunedrev_ref.h (about 3 MB)`. Check by eye: `grep -c "static const" tests/ref/stunedrev_ref.h` gives 8 and `tests/ref/seam_moorer_ref.h` 2; the first row of `kStdel96` (ms = 1) starts with `137` (√2 · 96 = 135.8 → 136 → 137) and ends with the π column's section 42 (≈ 12 667). If `faust` fails on `sff.np`, check that `-I "$SEAMLIBS/h"` reaches `../h/nextprime.h`.

- [ ] **Step 6: Commit.**

```bash
git add tests/stunedrev_burst.h tests/ref/stunedrev_ref.h tests/ref/seam_moorer_ref.h doc/study/sscdo2/stunedrev-plugin
git commit -m "test(stunedrev): Faust references of sdt.stdel, sjm.apfv, sdt.stunedrev"
```

---

### Task 3: `seam_moorer.h`, then the engine, part 1: delays and arena sizing

**Files:**
- Create: `plugins/_common/seam_moorer.h` (`Seam::MoorerAllpass`, the C++ side of `seam.moorer.lib`'s `apfv`)
- Create: `tests/seam_moorer_test.cpp`
- Create: `plugins/stunedrev/source/stunedrev_dsp.h` (constants, `sectionDelay`, `sectionLength`, `sieveBound`)
- Create: `tests/stunedrev_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Seam::PrimeSieve`, `Seam::msToPrimeSamples` (Task 1); `stunedrevref::kStdel96/48`, `moorerref::kApfv`, `kApfv07` (Task 2).
- Produces (namespace `stunedrev`): `kLines = 4`, `kSections = 42`, `kTMin = 1`, `kTMax = 100`, `kDefaultTimes[4] = {83,47,7,71}`, `kShortRamp = 0.025`, `kClearBytesPerSample = 16384`, `kG`, `kRatio[4]`; `uint32_t sectionDelay(int line, int i, double ms, double fs, const Seam::PrimeSieve&)`; `std::size_t sectionLength(int line, int i, double fs, const Seam::PrimeSieve&)`; `uint32_t sieveBound(double fs)`.
- Produces (`seam_moorer.h`): `class Seam::MoorerAllpass` with `void attach(double* buf, std::size_t len)`, `void clear()` (state to zero; the buffer is the caller's to zero), `void setDelay(uint32_t t)`, `uint32_t delay() const`, `void setGain(double g)`, `double gain() const`, `double tick(double x)`.

- [ ] **Step 0a: Write the failing library test** `tests/seam_moorer_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_moorer.h"
#include "ref/seam_moorer_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using Seam::MoorerAllpass;

static double impulseErr(double g, const double* ref) {
    std::vector<double> buf(1025, 0.0);
    MoorerAllpass a; a.attach(buf.data(), buf.size()); a.setDelay(37); a.setGain(g);
    double err = 0.0, pk = 0.0;
    for (int k = 0; k < 512; ++k) {
        const double y = a.tick(k == 0 ? 1.0 : 0.0);
        err = std::max(err, std::fabs(y - ref[k]));
        pk  = std::max(pk, std::fabs(ref[k]));
    }
    return err / pk;
}

TEST_CASE("impulse response equals sjm.apfv(1024, 37, 1/sqrt(2))") {
    CHECK(impulseErr(1.0 / std::sqrt(2.0), moorerref::kApfv) < 1e-15);
}

TEST_CASE("impulse response equals sjm.apfv(1024, 37, 0.7): g is a parameter") {
    CHECK(impulseErr(0.7, moorerref::kApfv07) < 1e-15);
}

TEST_CASE("all-pass by structure: the impulse response carries unit energy") {
    for (double g : {0.3, 0.7, 1.0 / std::sqrt(2.0), 0.95}) {
        std::vector<double> buf(64, 0.0);
        MoorerAllpass a; a.attach(buf.data(), buf.size()); a.setDelay(13); a.setGain(g);
        double e = 0.0;
        for (int k = 0; k < 200000; ++k) { const double y = a.tick(k == 0 ? 1.0 : 0.0); e += y * y; }
        CAPTURE(g);
        CHECK(std::fabs(e - 1.0) < 1e-9);
    }
}

TEST_CASE("clear() returns the state to zero; the caller's zeroed buffer completes it") {
    std::vector<double> buf(64, 0.0);
    MoorerAllpass a; a.attach(buf.data(), buf.size()); a.setDelay(13); a.setGain(0.7);
    for (int k = 0; k < 100; ++k) a.tick(k == 0 ? 1.0 : 0.0);
    std::fill(buf.begin(), buf.end(), 0.0);
    a.clear();
    std::vector<double> fbuf(64, 0.0);
    MoorerAllpass f; f.attach(fbuf.data(), fbuf.size()); f.setDelay(13); f.setGain(0.7);
    for (int k = 0; k < 300; ++k) CHECK(a.tick(k == 0 ? 1.0 : 0.0) == f.tick(k == 0 ? 1.0 : 0.0));
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# seam_moorer.h: Moorer's all-pass against sjm.apfv; references in
# ref/seam_moorer_ref.h (doc/study/sscdo2/stunedrev-plugin/gen-ref.sh).
add_executable(seam_moorer_test seam_moorer_test.cpp)
target_include_directories(seam_moorer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_moorer_test PRIVATE cxx_std_17)
add_test(NAME seam_moorer_test COMMAND seam_moorer_test)
```

Run: `cmake -S . -B build-test && cmake --build build-test --config Release --target seam_moorer_test 2>&1 | tail -3`
Expected: compile error, `seam_moorer.h` not found.

- [ ] **Step 0b: Write `plugins/_common/seam_moorer.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_moorer.h — Moorer's all-pass, the C++ side of seam.moorer.lib
//
// FAUST REFERENCE (seam.moorer.lib, sjm):
//   apfv(md,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(md,t-1),_)~(0-_) : mem+_;
//
// J. A. Moorer, "About This Reverberation Business" (1979), fig. 2(b): one
// multiplier, so the section is all-pass by structure for any g and any
// rounding. apfv is the form with the buffer as a parameter (written
// 2026-09-29 for Davide Tedesco's stunedrev); here the buffer belongs to the
// caller, so many sections can share one arena or each own a vector.
//
// Unrolled per sample, v being the delay's output held one sample by the
// loop (and by the output's mem):
//   a = -g·(x - v)      the only multiplier
//   w = a + x           written to the buffer
//   y = v + a
//   v = w[n-(t-1)]      read after the write, with the t of this sample
// so a new t is heard one sample after it is set, as in the Faust.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstddef>
#include <cstdint>

namespace Seam {

class MoorerAllpass {
public:
    // buf holds len doubles, zeroed by the caller; len >= the longest delay + 1.
    void attach(double* buf, std::size_t len) { buf_ = buf; len_ = len; clear(); }

    // The state to zero. The buffer is the caller's to zero (stunedrev's
    // RESET zeroes its arena in slices, over several blocks).
    void clear() { pos_ = 0; v_ = 0.0; }

    void     setDelay(uint32_t t) { t_ = t; }   // 1 <= t <= len - 1
    uint32_t delay() const        { return t_; }
    void     setGain(double g)    { g_ = g; }
    double   gain() const         { return g_; }

    double tick(double x) {
        const double a = -g_ * (x - v_);
        buf_[pos_] = a + x;
        const double y = v_ + a;
        std::size_t r = pos_ + len_ - (t_ - 1);
        if (r >= len_) r -= len_;
        v_ = buf_[r];
        if (++pos_ == len_) pos_ = 0;
        return y;
    }

private:
    double*     buf_ = nullptr;
    std::size_t len_ = 0, pos_ = 0;
    uint32_t    t_ = 1;
    double      g_ = 0.0;
    double      v_ = 0.0;
};

} // namespace Seam
```

Run: `cmake --build build-test --config Release --target seam_moorer_test && ctest --test-dir build-test -C Release -R seam_moorer_test --output-on-failure`
Expected: 4 cases passed. Commit:

```bash
git add plugins/_common/seam_moorer.h tests/seam_moorer_test.cpp tests/CMakeLists.txt
git commit -m "feat(common): seam_moorer.h, Moorer's all-pass as a reusable library (sjm.apfv)"
```

- [ ] **Step 1: Write the failing tests** `tests/stunedrev_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "stunedrev_dsp.h"
#include "stunedrev_burst.h"
#include "ref/stunedrev_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace stunedrev;

// ── Test 2 of the spec: the 16 800 delays equal sdt.stdel, exactly ─────────
static long delayMismatches(double fs, const int (*ref)[kLines * kSections]) {
    const Seam::PrimeSieve s(sieveBound(fs));
    long bad = 0;
    for (int ms = kTMin; ms <= kTMax; ++ms)
        for (int j = 0; j < kLines; ++j)
            for (int i = 0; i < kSections; ++i)
                if ((int)sectionDelay(j, i, ms, fs, s) != ref[ms - 1][j * kSections + i]) ++bad;
    return bad;
}

TEST_CASE("delays equal sdt.stdel at 96 kHz, every ms, section and line") {
    CHECK(delayMismatches(96000.0, stunedrevref::kStdel96) == 0);
}

TEST_CASE("delays equal sdt.stdel at 48 kHz") {
    CHECK(delayMismatches(48000.0, stunedrevref::kStdel48) == 0);
}

// ── Test 6: the arena holds every delay the slider can ask ────────────────
TEST_CASE("every section's length holds its delay at every ms, at every rate up to 384 kHz") {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0}) {
        const Seam::PrimeSieve s(sieveBound(fs));
        long bad = 0, zero = 0;
        for (int j = 0; j < kLines; ++j)
            for (int i = 0; i < kSections; ++i) {
                const std::size_t len = sectionLength(j, i, fs, s);
                for (int ms = kTMin; ms <= kTMax; ++ms) {
                    const uint32_t t = sectionDelay(j, i, ms, fs, s);
                    if (t == 0) ++zero;                 // the sieve ran out
                    if ((std::size_t)t + 1 > len) ++bad;
                }
            }
        CAPTURE(fs);
        CHECK(zero == 0);
        CHECK(bad == 0);
    }
}

TEST_CASE("the arena at 96 kHz is about 588 MiB, reported") {
    const Seam::PrimeSieve s(sieveBound(96000.0));
    std::size_t total = 0;
    for (int j = 0; j < kLines; ++j)
        for (int i = 0; i < kSections; ++i) total += sectionLength(j, i, 96000.0, s);
    const double mib = (double)total * sizeof(double) / (1024.0 * 1024.0);
    MESSAGE("arena at 96 kHz: " << total << " doubles, " << mib << " MiB");
    CHECK(mib > 580.0);
    CHECK(mib < 596.0);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# stunedrev: the engine against sdt.stunedrev; references in
# ref/stunedrev_ref.h (doc/study/sscdo2/stunedrev-plugin/gen-ref.sh).
add_executable(stunedrev_dsp_test stunedrev_dsp_test.cpp)
target_include_directories(stunedrev_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/stunedrev/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(stunedrev_dsp_test PRIVATE cxx_std_17)
add_test(NAME stunedrev_dsp_test COMMAND stunedrev_dsp_test)
```

- [ ] **Step 2: Run to see it fail.** Run: `cmake -S . -B build-test && cmake --build build-test --config Release --target stunedrev_dsp_test 2>&1 | tail -3`
Expected: compile error, `stunedrev_dsp.h` not found.

- [ ] **Step 3: Write the first part of `plugins/stunedrev/source/stunedrev_dsp.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the engine (SDK-free)
//
// Four independent lines of 42 Moorer all-pass sections in series, each
// line tuned by an irrational ratio k (sdt.stunedrev, seam.tedesco.lib).
// The delay of section i is ms·(i+1)·k, rounded to the sample and moved to
// the next prime at the session's rate (sdt.stdel); each section's buffer is
// sized exactly for the longest delay the slider can ask, 100 ms, in one
// arena allocated outside the audio thread.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_moorer.h"
#include "seam_primes.h"
#include "seam_ramp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace stunedrev {

constexpr int    kLines    = 4;
constexpr int    kSections = 42;
constexpr int    kTMin = 1, kTMax = 100;                     // ms, the score's slider
constexpr int    kDefaultTimes[kLines] = { 83, 47, 7, 71 };  // the Pd patch's start
constexpr double kShortRamp = 0.025;                          // s
// RESET zeroes the arena at this many bytes per sample of the block: the
// clearing lasts the same time at any block size and rate (588 MiB at
// 96 kHz in 0.39 s), and asks memset for 1.5 GB/s, a fraction of its speed.
constexpr std::size_t kClearBytesPerSample = 16384;

// g = 1/sqrt(2) as in the original; the ratios in the original's line order.
// ma.E and ma.PI are these doubles.
inline const double kG = 1.0 / std::sqrt(2.0);
inline const double kRatio[kLines] = {
    std::sqrt(2.0), (1.0 + std::sqrt(5.0)) / 2.0, 2.718281828459045, 3.141592653589793 };

// sdt.stdel(k, i, ms) = sma.ms2npsamp(ms*(i+1)*k): the product before the prime.
inline uint32_t sectionDelay(int line, int i, double ms, double fs, const Seam::PrimeSieve& s) {
    return Seam::msToPrimeSamples(ms * (i + 1) * kRatio[line], fs, s);
}

// Rounding and the prime above are both non-decreasing, so the longest
// delay of a section is the one at 100 ms; +1 holds w[n-(t-1)] with w[n].
// sdt.stmd sizes with a fixed +150 instead, because Faust sizes buffers at
// compile time and cannot evaluate sff.np there; exact sizing is right at
// any rate (at 384 kHz a prime gap of 154 would exceed the +150).
inline std::size_t sectionLength(int line, int i, double fs, const Seam::PrimeSieve& s) {
    return (std::size_t)sectionDelay(line, i, kTMax, fs, s) + 1;
}

// The largest n of the plugin (100 ms, section 42, pi) plus room for one
// prime gap: 1024 is above every gap below 2^32 (the largest is 336).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(kTMax * kSections * kRatio[3] * fs / 1000.0 + 0.5) + 1024u;
}

// Each section is a Seam::MoorerAllpass (seam_moorer.h), g = kG, attached
// to its slice of the arena.
using Section = Seam::MoorerAllpass;

} // namespace stunedrev
```

- [ ] **Step 4: Run to see it pass.** Run: `cmake --build build-test --config Release --target stunedrev_dsp_test && ctest --test-dir build-test -C Release -R stunedrev_dsp_test --output-on-failure -V | grep -E "arena|passed|failed"`
Expected: 4 test cases passed; the message prints the arena (about 588 MiB).
**If a delay test fails** on a handful of values, the C++ product order differs from the Faust compiler's at a rounding boundary: open the generated `ref.h` (rerun `faust ... dsp/stdel.dsp` by hand), read how it computes the argument of `floor` (for instance `fConst * fEntry0`), write `sectionDelay` in that order with a comment saying so, and rerun. Never loosen the test.

- [ ] **Step 5: Commit.**

```bash
git add plugins/stunedrev/source/stunedrev_dsp.h tests/stunedrev_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(stunedrev): delays and exact sizing, equal to the spec"
```

---

### Task 4: The engine, part 2: arena, lines, gains, process

**Files:**
- Modify: `plugins/stunedrev/source/stunedrev_dsp.h` (append `Status`, `Engine`)
- Modify: `tests/stunedrev_dsp_test.cpp` (append)

**Interfaces:**
- Consumes: everything of Task 3; `Seam::LinearRamp` (`setTarget(target, seconds, fs)`, `snap()`, `next()`, `value()`, `target()`, `active()`); `Burst`; `stunedrevref::kWin96`, `kWin48`, `kChange96` and their `_energy`.
- Produces: `enum class Status : int { Unprepared, Ready, Clearing, AllocFailed }`; `class Engine` with `bool prepare(double fs)`, `void release()`, `void reset()`, `void setTime(int line, int ms)`, `void setInput(double)`, `void setOutput(double)`, `void setPower(bool)`, `void requestReset()`, `template<class T> void process(const T* const* in, T* const* out, int n)`, `int time(int line) const`, `uint32_t delay(int line, int i) const`, `double centroidSeconds(int line) const`, `std::size_t arenaBytes() const`, `double sampleRate() const`, `Status status() const`. (The RESET state machine lands in Task 5; this task declares `requestReset` and the phase but tests only the running path.)

- [ ] **Step 1: Append the failing tests** to `tests/stunedrev_dsp_test.cpp`:

```cpp
// ── The engine against sdt.stunedrev ──────────────────────────────────────
static void settle(Engine& e, double fs) {
    REQUIRE(e.prepare(fs));
    e.setInput(1.0); e.setOutput(1.0); e.setPower(true);
    e.reset();                              // every ramp onto its target
}

struct Capture {
    int seconds; long fs;
    std::vector<double> win, energy;        // [line][second][512], [line][second]
    Capture(int s, long rate) : seconds(s), fs(rate), win((size_t)4 * s * 512, 0.0), energy((size_t)4 * s, 0.0) {}
    double& w(int c, int s, int k) { return win[((size_t)c * seconds + s) * 512 + k]; }
    double& e(int c, int s) { return energy[(size_t)c * seconds + s]; }
};

// The burst through the engine in blocks of `block`; hook(pos) runs before
// the block that starts at pos. inPlace feeds the same buffers in and out.
template <class Hook>
static Capture run(Engine& e, long fs, int seconds, int block, Hook hook, bool inPlace = false) {
    Capture cap(seconds, fs);
    Burst burst((double)fs);
    std::vector<double> ib((size_t)4 * block), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) {
        in[c]  = ib.data() + (size_t)c * block;
        out[c] = inPlace ? in[c] : ob.data() + (size_t)c * block;
    }
    const long total = fs * seconds;
    for (long pos = 0; pos < total; pos += block) {
        const int m = (int)std::min<long>(block, total - pos);
        hook(pos);
        burst.fill(in, m);
        e.process(in, out, m);
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < m; ++k) {
                const long g = pos + k; const int s = (int)(g / fs); const long off = g % fs;
                const double y = out[c][k];
                if (off < 512) cap.w(c, s, (int)off) = y;
                cap.e(c, s) += y * y;
            }
    }
    return cap;
}
static Capture run(Engine& e, long fs, int seconds, int block = 256) {
    return run(e, fs, seconds, block, [](long) {});
}

template <int S>
static double winErr(Capture& c, const double (*ref)[S][512]) {
    double err = 0.0, pk = 0.0;
    for (int l = 0; l < 4; ++l) for (int s = 0; s < S; ++s) for (int k = 0; k < 512; ++k) {
        err = std::max(err, std::fabs(c.w(l, s, k) - ref[l][s][k]));
        pk  = std::max(pk, std::fabs(ref[l][s][k]));
    }
    return err / pk;
}
template <int S>
static double energyErr(Capture& c, const double (*ref)[S]) {
    double err = 0.0, pk = 0.0;
    for (int l = 0; l < 4; ++l) for (int s = 0; s < S; ++s) {
        err = std::max(err, std::fabs(c.e(l, s) - ref[l][s]));
        pk  = std::max(pk, ref[l][s]);
    }
    return err / pk;
}

// Test 4 of the spec.
TEST_CASE("the engine equals sdt.stunedrev(83, 47, 7, 71) at 96 kHz over 30 s") {
    Engine e; settle(e, 96000.0);
    Capture c = run(e, 96000, 30);
    CHECK(winErr<30>(c, stunedrevref::kWin96) < 1e-12);
    CHECK(energyErr<30>(c, stunedrevref::kWin96_energy) < 1e-10);
}

TEST_CASE("the engine equals sdt.stunedrev at 48 kHz over 30 s") {
    Engine e; settle(e, 48000.0);
    Capture c = run(e, 48000, 30);
    CHECK(winErr<30>(c, stunedrevref::kWin48) < 1e-12);
    CHECK(energyErr<30>(c, stunedrevref::kWin48_energy) < 1e-10);
}

// Test 5: a time moved at a block boundary.
TEST_CASE("a change of time equals the spec's: t3 7 -> 9 ms at sample 48128") {
    Engine e; settle(e, 96000.0);
    Capture c = run(e, 96000, 3, 256, [&](long pos) { if (pos == 48128) e.setTime(2, 9); });
    CHECK(winErr<3>(c, stunedrevref::kChange96) < 1e-12);
    CHECK(energyErr<3>(c, stunedrevref::kChange96_energy) < 1e-10);
}

// Test 9: the centroid is the 96 kHz time scale, to within a prime gap per section.
TEST_CASE("centroid: never below the exact time, above it by at most one prime gap per section") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Engine e; settle(e, fs);
        for (int j = 0; j < kLines; ++j) {
            // sum over i of ms·(i+1)·k = ms·k·903
            const double exact = kDefaultTimes[j] * kRatio[j] * 903.0 / 1000.0;
            const double c = e.centroidSeconds(j);
            CAPTURE(fs); CAPTURE(j);
            CHECK(c >= exact);
            CHECK(c - exact <= kSections * 155.0 / fs);   // rounding + gap < 155 samples
        }
    }
    Engine e; settle(e, 96000.0);
    CHECK(std::fabs(e.centroidSeconds(0) - 106.0) < 0.1);   // the report's 106.0 s
}

// Review focus 1, 2, 3, 5.
TEST_CASE("in-place buffers give the out-of-place output exactly") {
    Engine a; settle(a, 48000.0);
    Engine b; settle(b, 48000.0);
    Capture ca = run(a, 48000, 2);
    Capture cb = run(b, 48000, 2, 256, [](long) {}, true);
    CHECK(ca.win == cb.win);
    CHECK(ca.energy == cb.energy);
}

TEST_CASE("block partition: 1, 7 and 4093-sample blocks equal 256-sample blocks exactly") {
    Engine ref; settle(ref, 48000.0);
    Capture cr = run(ref, 48000, 2);
    for (int block : {1, 7, 4093}) {
        Engine e; settle(e, 48000.0);
        Capture c = run(e, 48000, 2, block);
        CAPTURE(block);
        CHECK(c.win == cr.win);
        CHECK(c.energy == cr.energy);
    }
}

TEST_CASE("prepare at a new rate gives a fresh engine at that rate") {
    Engine e; settle(e, 96000.0);
    run(e, 96000, 1);                       // memory full of the burst
    settle(e, 48000.0);                     // the host's setActive(false/true) with a new rate
    Engine fresh; settle(fresh, 48000.0);
    for (int j = 0; j < kLines; ++j) {
        CHECK(e.centroidSeconds(j) == fresh.centroidSeconds(j));
        for (int i = 0; i < kSections; ++i) CHECK(e.delay(j, i) == fresh.delay(j, i));
    }
    Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
    CHECK(a.win == b.win);
}

TEST_CASE("without memory process() writes zeros") {
    Engine e;                                // never prepared
    double ib[4][64], ob[4][64];
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib[c]; out[c] = ob[c]; std::fill(ib[c], ib[c] + 64, 1.0); std::fill(ob[c], ob[c] + 64, 9.0); }
    e.process(in, out, 64);
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 64; ++k) CHECK(ob[c][k] == 0.0);
    CHECK(e.status() == Status::Unprepared);
    settle(e, 48000.0);
    e.release();
    std::fill(ob[0], ob[0] + 64, 9.0);
    e.process(in, out, 64);
    CHECK(ob[0][10] == 0.0);
}

TEST_CASE("POWER off: after its 25 ms ramp the output is exactly zero; the lines keep running") {
    Engine e;  settle(e, 48000.0);
    Engine on; settle(on, 48000.0);
    e.setPower(false);                        // off from the start, back on at sample 48128
    Burst be(48000.0), bo(48000.0);
    const int B = 256;
    std::vector<double> ie(4 * B), oe(4 * B), io(4 * B), oo(4 * B);
    double *pie[4], *poe[4], *pio[4], *poo[4];
    for (int c = 0; c < 4; ++c) {
        pie[c] = ie.data() + c * B; poe[c] = oe.data() + c * B;
        pio[c] = io.data() + c * B; poo[c] = oo.data() + c * B;
    }
    long nonzero = 0; double err = 0.0;
    for (long pos = 0; pos < 96000; pos += B) {
        if (pos == 48128) e.setPower(true);
        be.fill(pie, B); bo.fill(pio, B);
        e.process(pie, poe, B); on.process(pio, poo, B);
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < B; ++k) {
                const long g = pos + k;
                if (g >= 1200 && g < 48128 && poe[c][k] != 0.0) ++nonzero;      // ramp of 1200 samples
                if (g >= 48128 + 1200) err = std::max(err, std::fabs(poe[c][k] - poo[c][k]));
            }
    }
    CHECK(nonzero == 0);
    CHECK(err == 0.0);   // the memory kept turning while POWER was off
}
```

- [ ] **Step 2: Run to see them fail.** Run: `cmake --build build-test --config Release --target stunedrev_dsp_test 2>&1 | tail -3`
Expected: compile errors, `Engine` and `Status` undeclared.

- [ ] **Step 3: Append the engine** to `stunedrev_dsp.h`, inside `namespace stunedrev`, after `Section`:

```cpp
enum class Status : int { Unprepared = 0, Ready, Clearing, AllocFailed };

class Engine {
public:
    Engine() { for (int j = 0; j < kLines; ++j) time_[j] = kDefaultTimes[j]; }

    // Outside the audio thread (setActive). Sieve, arena (zeroed now, so
    // every page is resident before process() runs), delays. false when the
    // memory is not there: the engine stays silent.
    bool prepare(double fs) {
        release();
        fs_ = fs;
        sampleRate_.store(fs);
        try {
            sieve_.reset(new Seam::PrimeSieve(sieveBound(fs)));
            std::size_t total = 0;
            for (int j = 0; j < kLines; ++j)
                for (int i = 0; i < kSections; ++i) total += sectionLength(j, i, fs, *sieve_);
            arena_.reset(new double[total]());
            arenaSize_ = total;
        } catch (const std::bad_alloc&) {
            release();
            status_.store(Status::AllocFailed);
            return false;
        }
        std::size_t off = 0;
        for (int j = 0; j < kLines; ++j)
            for (int i = 0; i < kSections; ++i) {
                const std::size_t len = sectionLength(j, i, fs, *sieve_);
                sec_[j][i].attach(arena_.get() + off, len);
                sec_[j][i].setGain(kG);
                off += len;
            }
        arenaBytes_.store(arenaSize_ * sizeof(double));
        for (int j = 0; j < kLines; ++j) applyTime(j);
        in_.setTarget(in_.target(), kShortRamp, fs_);
        out_.setTarget(out_.target(), kShortRamp, fs_);
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        fade_.setTarget(1.0, kShortRamp, fs_);
        phase_ = Phase::Run;
        clearPos_ = 0;
        servedGen_ = resetGen_.load();       // a click before prepare is not replayed
        status_.store(Status::Ready);
        return true;
    }

    void release() {
        arena_.reset();
        sieve_.reset();
        arenaSize_ = 0;
        arenaBytes_.store(0);
        status_.store(Status::Unprepared);
    }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { in_.snap(); out_.snap(); pow_.snap(); fade_.snap(); }

    // Audio thread, at the start of a block (or before prepare). The delays
    // jump, as in the spec: the buffers hold the whole history.
    void setTime(int line, int ms) {
        ms = std::min(kTMax, std::max(kTMin, ms));
        if (ms == time_[line] && applied_[line]) return;
        time_[line] = ms;
        if (arena_) applyTime(line);
    }
    void setInput(double v)  { if (v != in_.target())  in_.setTarget(v, kShortRamp, fs_); }
    void setOutput(double v) { if (v != out_.target()) out_.setTarget(v, kShortRamp, fs_); }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    // Any thread (the RESET view): served at the start of the next block.
    void requestReset() { resetGen_.fetch_add(1); }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!arena_) { zero(out, n); return; }
        // RESET: Task 5 serves resetGen_ here.
        const bool feeding = phase_ == Phase::Run;
        for (int k = 0; k < n; ++k) {
            const double gin  = in_.next();
            const double gout = out_.next() * pow_.next() * fade_.next();
            double x[kLines];
            for (int j = 0; j < kLines; ++j) x[j] = feeding ? (double)in[j][k] * gin : 0.0;
            for (int j = 0; j < kLines; ++j) {
                double y = x[j];
                for (Section& s : sec_[j]) y = s.tick(y);
                out[j][k] = (T)(y * gout);
            }
        }
    }

    // Readouts.
    int         time(int line) const           { return time_[line]; }
    uint32_t    delay(int line, int i) const   { return sec_[line][i].delay(); }
    double      centroidSeconds(int line) const { return centroid_[line].load(); }
    std::size_t arenaBytes() const             { return arenaBytes_.load(); }
    double      sampleRate() const             { return sampleRate_.load(); }
    Status      status() const                 { return status_.load(); }

private:
    enum class Phase { Run, FadeOut, Clear };

    void applyTime(int line) {
        double sum = 0.0;
        for (int i = 0; i < kSections; ++i) {
            const uint32_t t = sectionDelay(line, i, time_[line], fs_, *sieve_);
            sec_[line][i].setDelay(t);
            sum += t;
        }
        centroid_[line].store(sum / fs_);   // each section delays the energy by its t on average
        applied_[line] = true;
    }

    template <class T>
    static void zero(T* const* out, int n) {
        for (int j = 0; j < kLines; ++j) std::fill(out[j], out[j] + n, (T)0);
    }

    double fs_ = 96000.0;
    int    time_[kLines] = {};
    bool   applied_[kLines] = {};
    Section sec_[kLines][kSections];
    std::unique_ptr<double[]> arena_;
    std::size_t arenaSize_ = 0, clearPos_ = 0;
    std::unique_ptr<Seam::PrimeSieve> sieve_;
    Seam::LinearRamp in_, out_, pow_, fade_;
    Phase phase_ = Phase::Run;
    uint32_t servedGen_ = 0;

    std::atomic<uint32_t>    resetGen_{0};
    std::atomic<Status>      status_{Status::Unprepared};
    std::atomic<double>      centroid_[kLines] = {};
    std::atomic<std::size_t> arenaBytes_{0};
    std::atomic<double>      sampleRate_{0.0};
};
```

Note on `prepare`: `applied_` must be reset so that `applyTime` runs for every line; `applyTime` is called directly there, so it does. `setTime` before `prepare` stores the time only.

- [ ] **Step 4: Run to see them pass.** Run: `cmake --build build-test --config Release --target stunedrev_dsp_test && ctest --test-dir build-test -C Release -R stunedrev_dsp_test --output-on-failure`
Expected: 14 test cases passed. Write down the two relative errors (rerun with `-V` after temporarily adding `MESSAGE` lines if needed) for the README.
**If 96/48 kHz fail and the delay and `seam_moorer` tests pass:** compare `kWin96[2][0]` (line e, first second) sample by sample with the C++ to find the first differing sample; suspect the wiring (gain not set on a section, a section attached to the wrong slice, the line order) before the library.

- [ ] **Step 5: Commit.**

```bash
git add plugins/stunedrev/source/stunedrev_dsp.h tests/stunedrev_dsp_test.cpp
git commit -m "feat(stunedrev): the engine, equal to sdt.stunedrev at 96 and 48 kHz and across a change of time"
```

---

### Task 5: RESET

**Files:**
- Modify: `plugins/stunedrev/source/stunedrev_dsp.h` (serve `resetGen_`, `clearChunk`)
- Modify: `tests/stunedrev_dsp_test.cpp` (append)

**Interfaces:**
- Consumes: `Engine` of Task 4.
- Produces: the RESET behaviour of the spec: on a new generation, output fade to 0 over 25 ms with the input ignored; then `kClearBytesPerSample · n` bytes zeroed per block of n samples with zero output and frozen lines; then every section's `clear()`, fade back to 1, `Status::Ready`. `status()` is `Clearing` from the request to the end of the zeroing.

- [ ] **Step 1: Append the failing tests:**

```cpp
// ── Test 7: RESET ─────────────────────────────────────────────────────────
// Run zeros through e until the clearing has ended and the fade is back.
static void finishReset(Engine& e, double fs, int block) {
    std::vector<double> ib((size_t)4 * block, 0.0), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = ob.data() + (size_t)c * block; }
    long guard = 0;
    do { e.process(in, out, block); } while (e.status() == Status::Clearing && ++guard < 10000000);
    REQUIRE(e.status() == Status::Ready);
    const int tail = (int)(0.05 * fs);       // the 25 ms fade back, and more
    for (int done = 0; done < tail; done += block) e.process(in, out, block);
}

TEST_CASE("after RESET the engine sounds as a fresh one, exactly") {
    for (int block : {256, 1, 4093}) {
        Engine e; settle(e, 48000.0);
        run(e, 48000, 1);                     // the memory holds the burst
        e.requestReset();
        finishReset(e, 48000.0, block);
        Engine fresh; settle(fresh, 48000.0);
        Capture a = run(e, 48000, 2), b = run(fresh, 48000, 2);
        CAPTURE(block);
        CHECK(a.win == b.win);
        CHECK(a.energy == b.energy);
    }
}

TEST_CASE("RESET: clearing lasts the same time at any block size, about 0.39 s at 96 kHz") {
    Engine e; settle(e, 96000.0);
    e.requestReset();
    std::vector<double> ib(4 * 64, 0.0), ob(4 * 64);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 64; out[c] = ob.data() + c * 64; }
    long samples = 0;
    do { e.process(in, out, 64); samples += 64; } while (e.status() == Status::Clearing);
    const double seconds = samples / 96000.0;
    MESSAGE("RESET at 96 kHz: " << seconds << " s");
    CHECK(seconds > 0.3);
    CHECK(seconds < 0.5);
}

TEST_CASE("RESET silences the output during the clearing and ignores the input") {
    Engine e; settle(e, 48000.0);
    run(e, 48000, 1);
    e.requestReset();
    // 25 ms fade, then the clearing: from the first block after the fade
    // the output is exactly zero while a full-scale input arrives.
    std::vector<double> ib(4 * 256, 1.0), ob(4 * 256);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 256; out[c] = ob.data() + c * 256; }
    for (int b = 0; b < 6; ++b) e.process(in, out, 256);       // 1536 samples > 1200 of fade
    long nonzero = 0;
    while (e.status() == Status::Clearing) {
        e.process(in, out, 256);
        if (e.status() == Status::Clearing)
            for (int c = 0; c < 4; ++c) for (int k = 0; k < 256; ++k) if (out[c][k] != 0.0) ++nonzero;
    }
    CHECK(nonzero == 0);
}

TEST_CASE("RESET twice: a click during the clearing restarts it") {
    Engine e; settle(e, 96000.0);
    std::vector<double> ib(4 * 256, 0.0), ob(4 * 256);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 256; out[c] = ob.data() + c * 256; }
    e.requestReset();
    long first = 0;
    for (int b = 0; b < 40; ++b) { e.process(in, out, 256); first += 256; }
    REQUIRE(e.status() == Status::Clearing);
    e.requestReset();                         // the second click
    long second = 0;
    do { e.process(in, out, 256); second += 256; } while (e.status() == Status::Clearing);
    CHECK(second > (long)(0.3 * 96000.0));    // a whole clearing again, not the rest of the first
}

TEST_CASE("RESET with POWER off clears and stays silent; a click before prepare is dropped") {
    Engine e;
    e.requestReset();                         // before prepare
    settle(e, 48000.0);
    CHECK(e.status() == Status::Ready);
    run(e, 48000, 1);
    e.setPower(false);
    e.requestReset();
    finishReset(e, 48000.0, 256);
    Capture c = run(e, 48000, 1);
    double sum = 0.0;
    for (int l = 0; l < 4; ++l) sum += c.e(l, 0);
    CHECK(sum == 0.0);
}
```

- [ ] **Step 2: Run to see them fail.** Run: `cmake --build build-test --config Release --target stunedrev_dsp_test && ctest --test-dir build-test -C Release -R stunedrev_dsp_test --output-on-failure 2>&1 | tail -15`
Expected: the RESET cases fail (`status()` never becomes `Clearing`; the "fresh" comparison differs).

- [ ] **Step 3: Serve RESET.** In `Engine::process`, replace the line `// RESET: Task 5 serves resetGen_ here.` with:

```cpp
        const uint32_t gen = resetGen_.load();
        if (gen != servedGen_) {                  // a click: fade, then clear
            servedGen_ = gen;
            phase_ = Phase::FadeOut;
            fade_.setTarget(0.0, kShortRamp, fs_);
            status_.store(Status::Clearing);
        }
        if (phase_ == Phase::Clear) {
            clearChunk(n);
            for (int k = 0; k < n; ++k) { in_.next(); out_.next(); pow_.next(); }
            zero(out, n);
            return;
        }
```

and at the end of `process`, after the sample loop:

```cpp
        if (phase_ == Phase::FadeOut && !fade_.active()) {
            phase_ = Phase::Clear;                // the lines freeze from the next block
            clearPos_ = 0;
        }
```

Add to the private part of `Engine`:

```cpp
    // A slice of the arena per block, proportional to the block: the lines
    // are frozen, so no uncleared history can flow into a cleared buffer.
    void clearChunk(int n) {
        const std::size_t want = (std::size_t)n * kClearBytesPerSample / sizeof(double);
        const std::size_t m = std::min(want, arenaSize_ - clearPos_);
        std::memset(arena_.get() + clearPos_, 0, m * sizeof(double));
        clearPos_ += m;
        if (clearPos_ < arenaSize_) return;
        for (auto& line : sec_) for (Section& s : line) s.clear();
        phase_ = Phase::Run;
        fade_.setTarget(1.0, kShortRamp, fs_);
        status_.store(Status::Ready);
    }
```

- [ ] **Step 4: Run to see them pass.** Run: `ctest --test-dir build-test -C Release -R stunedrev_dsp_test --output-on-failure -V 2>&1 | grep -E "RESET at|passed|failed"`
Expected: every case passed; the message prints the clearing time (about 0.39 s).

- [ ] **Step 5: Commit.**

```bash
git add plugins/stunedrev/source/stunedrev_dsp.h tests/stunedrev_dsp_test.cpp
git commit -m "feat(stunedrev): RESET empties the memory while playing, at a fixed rate per sample"
```

---

### Task 6: Parameters and state

**Files:**
- Create: `plugins/stunedrev/source/stunedrev_params.h` (SDK-free)
- Create: `plugins/stunedrev/source/stunedrev_state.h` (SDK: the IBStream codec)
- Create: `tests/stunedrev_params_test.cpp`, `tests/stunedrev_state_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Engine` setters (Task 4); `Seam::readStateDoubles(IBStream*, double*, int)` (`seam_state.h`).
- Produces: `enum class Param : int { Power = 0, T1, T2, T3, T4, Input, Output }`, `kNumParams = 7`, `double timeToNormalized(int ms)`, `int normalizedToTime(double)`, `double defaultNormalized(Param)`, `struct Plain { bool power; int t[4]; double input, output; }`, `class ParamBox { store, normalized, plain }`, `void applyTo(const Plain&, Engine&)`; `void writeState(Steinberg::IBStream*, const ParamBox&)`, `int readState(Steinberg::IBStream*, ParamBox&)`.

- [ ] **Step 1: Write the failing tests.**

`tests/stunedrev_params_test.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "stunedrev_params.h"
#include <cmath>

using namespace stunedrev;

TEST_CASE("defaults: the Pd patch's times, POWER off, input and output at 0") {
    ParamBox box;
    const Plain p = box.plain();
    CHECK_FALSE(p.power);
    for (int j = 0; j < kLines; ++j) CHECK(p.t[j] == kDefaultTimes[j]);
    CHECK(p.input == 0.0);
    CHECK(p.output == 0.0);
}

TEST_CASE("every millisecond survives normalized and back") {
    for (int ms = kTMin; ms <= kTMax; ++ms) CHECK(normalizedToTime(timeToNormalized(ms)) == ms);
}

TEST_CASE("off-grid host values map as the SDK's RangeParameter displays them") {
    // RangeParameter with stepCount 99: plain = min + min(99, int(norm * 100)).
    for (int k = 0; k <= 1000; ++k) {
        const double norm = k / 1000.0;
        const int sdk = kTMin + std::min(99, (int)(norm * 100.0));
        CHECK(normalizedToTime(norm) == sdk);
    }
}

TEST_CASE("applyTo hands every value to the engine") {
    ParamBox box;
    box.store(Param::Power, 1.0);
    box.store(Param::T3, timeToNormalized(9));
    box.store(Param::Input, 0.5);
    box.store(Param::Output, 0.25);
    Engine e;
    REQUIRE(e.prepare(48000.0));
    applyTo(box.plain(), e);
    CHECK(e.time(2) == 9);
    CHECK(e.time(0) == 83);
}
```

`tests/stunedrev_state_test.cpp`:
```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "stunedrev_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace stunedrev;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the processor's state round-trips every parameter") {
    ParamBox a;
    a.store(Param::Power, 1.0);
    a.store(Param::T1, timeToNormalized(12));
    a.store(Param::T4, timeToNormalized(100));
    a.store(Param::Input, 0.3);
    a.store(Param::Output, 0.9);
    MemoryStream stream;
    writeState(&stream, a);
    rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == kNumParams);
    for (int i = 0; i < kNumParams; ++i) CHECK(b.normalized((Param)i) == a.normalized((Param)i));
}

TEST_CASE("a short blob keeps the defaults for the fields it lacks") {
    MemoryStream stream;
    {
        Steinberg::IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(1.0);                          // Power only
    }
    rewindStream(stream);
    ParamBox b;
    b.store(Param::T2, timeToNormalized(5));        // a value the recall must replace by the default
    CHECK(readState(&stream, b) == 1);
    CHECK(b.plain().power);
    CHECK(b.plain().t[1] == kDefaultTimes[1]);
}
```

Append to `tests/CMakeLists.txt`:
```cmake
add_executable(stunedrev_params_test stunedrev_params_test.cpp)
target_include_directories(stunedrev_params_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/stunedrev/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(stunedrev_params_test PRIVATE cxx_std_17)
add_test(NAME stunedrev_params_test COMMAND stunedrev_params_test)

# stunedrev_state_test links the SDK base layers, as seam_state_test does:
# the unit under test is the processor's IBStream codec.
add_executable(stunedrev_state_test
    stunedrev_state_test.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
target_include_directories(stunedrev_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/stunedrev/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${vst3sdk_SOURCE_DIR}
)
target_link_libraries(stunedrev_state_test PRIVATE base sdk_common pluginterfaces)
if(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    target_link_libraries(stunedrev_state_test PRIVATE ${CORE_FOUNDATION})
endif()
target_compile_features(stunedrev_state_test PRIVATE cxx_std_17)
add_test(NAME stunedrev_state_test COMMAND stunedrev_state_test)
```

- [ ] **Step 2: Run to see them fail.** Run: `cmake -S . -B build-test && cmake --build build-test --config Release --target stunedrev_params_test stunedrev_state_test 2>&1 | tail -3`
Expected: compile errors, the headers do not exist.

- [ ] **Step 3: Write `plugins/stunedrev/source/stunedrev_params.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the parameters between the threads (SDK-free)
//
// The seven controls live here as normalized values in atomics, as in LMO:
// process() stores what the host's queues bring and reads the box; setState
// stores a recalled preset from the UI thread. Neither touches the SDK's
// Parameter objects from the audio thread (Parameter::setNormalized notifies
// the editor synchronously). No control depends on another, so a recall may
// store them in any order.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "stunedrev_dsp.h"
#include <algorithm>
#include <atomic>

namespace stunedrev {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, T1, T2, T3, T4, Input, Output };
constexpr int kNumParams = 7;
constexpr int kTimeSteps = kTMax - kTMin;   // 99, the RangeParameter's stepCount

inline double timeToNormalized(int ms) { return (double)(ms - kTMin) / kTimeSteps; }

// The SDK's RangeParameter::toPlain for a stepped parameter, so that the
// millisecond the DSP uses is the one the host displays, also for a host
// value between two steps.
inline int normalizedToTime(double norm) {
    norm = std::min(1.0, std::max(0.0, norm));
    return kTMin + std::min(kTimeSteps, (int)(norm * (kTimeSteps + 1)));
}

inline double defaultNormalized(Param p) {
    const int i = (int)p - (int)Param::T1;
    return (i >= 0 && i < kLines) ? timeToNormalized(kDefaultTimes[i]) : 0.0;
}

struct Plain {
    bool   power;
    int    t[kLines];
    double input, output;
};

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power = normalized(Param::Power) >= 0.5;
        for (int j = 0; j < kLines; ++j) x.t[j] = normalizedToTime(normalized((Param)((int)Param::T1 + j)));
        x.input  = normalized(Param::Input);
        x.output = normalized(Param::Output);
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    for (int j = 0; j < kLines; ++j) e.setTime(j, p.t[j]);
    e.setInput(p.input);
    e.setOutput(p.output);
    e.setPower(p.power);
}

} // namespace stunedrev
```

`plugins/stunedrev/source/stunedrev_state.h`:
```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the processor's state (SDK)
//
// Seven normalized doubles in Param order, little-endian, under the suite's
// append-only contract (seam_state.h): a short blob keeps the defaults for
// the fields it lacks. RESET is not here: it is not a plugin state.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "stunedrev_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace stunedrev {

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

// Returns how many fields the blob held; every field is stored, the missing
// ones as their defaults.
inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kNumParams);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);
    return n;
}

} // namespace stunedrev
```

- [ ] **Step 4: Run to see them pass.** Run: `cmake --build build-test --config Release --target stunedrev_params_test stunedrev_state_test && ctest --test-dir build-test -C Release -R "stunedrev_(params|state)_test" --output-on-failure`
Expected: 4 + 2 cases passed.

- [ ] **Step 5: Commit.**

```bash
git add plugins/stunedrev/source/stunedrev_params.h plugins/stunedrev/source/stunedrev_state.h tests/stunedrev_params_test.cpp tests/stunedrev_state_test.cpp tests/CMakeLists.txt
git commit -m "feat(stunedrev): ParamBox of atomics and the state codec, with the SDK's stepping"
```

---

### Task 7: The processor, the views, the window, the build

**Files:**
- Create: `plugins/stunedrev/CMakeLists.txt`
- Create: `plugins/stunedrev/source/stunedrev_ids.h`, `version.h`, `stunedrev_processor.h`, `stunedrev_processor.cpp`, `stunedrev_views.h`
- Create: `plugins/stunedrev/resource/stunedrev.uidesc`
- Modify: `CMakeLists.txt:129` (add `add_subdirectory(plugins/stunedrev)` after `plugins/lmo`)

**Interfaces:**
- Consumes: `Engine`, `ParamBox`, `applyTo`, `writeState`, `readState`.
- Produces: the `stunedrev.vst3` bundle; custom views named `StunedrevReset` and `StunedrevFooter`.

- [ ] **Step 1: `stunedrev_ids.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 18th plugin in the suite. word3 = ASCII "STR\0".
static const Steinberg::FUID StunedrevProcessorUID (0x5E4D0011, 0xA1B2C3D4, 0x53545200, 0x00000011);

enum StunedrevParams : Steinberg::Vst::ParamID {
    kParamPower  = 100,   // off / on   (100 + stunedrev::Param index)
    kParamT1     = 101,   // ms, line sqrt(2)
    kParamT2     = 102,   // ms, line phi
    kParamT3     = 103,   // ms, line e
    kParamT4     = 104,   // ms, line pi
    kParamInput  = 105,   // linear, CC83 in the original
    kParamOutput = 106    // linear, CC84 in the original
};

// Ranges and defaults: stunedrev_params.h (SDK-free, shared with the tests).

} // namespace Seam
```

- [ ] **Step 2: `version.h`** (copy of LMO's with the names changed):

```cpp
//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — Version and metadata
//─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"

#define stringOriginalFilename  "stunedrev.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "SEAM STUNEDREV – SSCDO#2 tuned all-pass memories (64Bit)"
#else
#define stringFileDescription   "SEAM STUNEDREV – SSCDO#2 tuned all-pass memories"
#endif
#define stringCompanyWeb        "https://s-e-a-m.github.io"
#define stringCompanyEmail      "mailto:seam@example.com"
#define stringCompanyName       "SEAM"
#define stringLegalCopyright    "© 2026 Giuseppe Silvi – GPL-3.0"
#define stringLegalTrademarks   ""
```

- [ ] **Step 3: `stunedrev_views.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — the two views the uidesc cannot describe
//
// RESET is not a parameter: a momentary parameter is lost when the host
// coalesces 0->1->0 into one point (ltglide, Reaper). The plugin is a
// SingleComponentEffect, so the view reaches the engine directly and asks
// it to empty the memory (a generation counter). The square is filled while
// the engine clears.
//
// The footer draws what the engine reports, read from its atomics by a GUI
// timer: the energy centroid of each line, the arena and the status.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cvstguitimer.h"
#include "stunedrev_dsp.h"

#include <cmath>
#include <cstdio>

namespace Seam {

class StunedrevResetButton : public VSTGUI::CView {
public:
    static constexpr double kBoxPx = 12.0;   // matched to the POWER CCheckBox

    StunedrevResetButton(const VSTGUI::CRect& size, stunedrev::Engine* engine,
                         const VSTGUI::CColor& frame, const VSTGUI::CColor& idle,
                         const VSTGUI::CColor& active)
        : CView(size), engine_(engine), frame_(frame), idle_(idle), active_(active) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 50, true);
    }
    ~StunedrevResetButton() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        const bool lit = pressed_ || (engine_ && engine_->status() == stunedrev::Status::Clearing);
        const CRect vs = getViewSize();
        CRect r(0, 0, kBoxPx, kBoxPx);
        r.offset(vs.left + 1.0, vs.top + std::ceil((vs.getHeight() - kBoxPx) / 2.0));
        c->setDrawMode(kAntiAliasing);
        c->setFrameColor(frame_);
        c->setFillColor(lit ? active_ : idle_);
        c->setLineWidth(1.0);
        c->drawRect(r, kDrawFilledAndStroked);
        setDirty(false);
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint&, const VSTGUI::CButtonState& b) override {
        if (!b.isLeftButton()) return VSTGUI::kMouseEventNotHandled;
        pressed_ = true; invalid();
        return VSTGUI::kMouseEventHandled;
    }
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where, const VSTGUI::CButtonState&) override {
        if (!pressed_) return VSTGUI::kMouseEventNotHandled;
        pressed_ = false;
        if (engine_ && getViewSize().pointInside(where)) engine_->requestReset();
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

private:
    stunedrev::Engine* engine_;
    VSTGUI::CColor frame_, idle_, active_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    bool pressed_ = false;
};

class StunedrevFooter : public VSTGUI::CView {
public:
    StunedrevFooter(const VSTGUI::CRect& size, const stunedrev::Engine* engine,
                    VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : CView(size), engine_(engine), font_(font), color_(color) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 200, true);
    }
    ~StunedrevFooter() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        char l1[192], l2[128];
        const stunedrev::Status st = engine_->status();
        if (st == stunedrev::Status::Ready || st == stunedrev::Status::Clearing) {
            std::snprintf(l1, sizeof l1,
                "memory  \xE2\x88\x9A" "2 %.0f s \xC2\xB7 \xCF\x86 %.0f s \xC2\xB7 e %.0f s \xC2\xB7 \xCF\x80 %.0f s",
                engine_->centroidSeconds(0), engine_->centroidSeconds(1),
                engine_->centroidSeconds(2), engine_->centroidSeconds(3));
        } else {
            std::snprintf(l1, sizeof l1, "memory  \xE2\x80\x94");
        }
        const char* status = st == stunedrev::Status::Ready ? "ready"
                           : st == stunedrev::Status::Clearing ? "clearing\xE2\x80\xA6"
                           : st == stunedrev::Status::AllocFailed ? "allocation failed" : "inactive";
        std::snprintf(l2, sizeof l2, "arena %.0f MiB @ %.1f kHz \xC2\xB7 %s",
                      engine_->arenaBytes() / (1024.0 * 1024.0), engine_->sampleRate() / 1000.0, status);
        const CRect vs = getViewSize();
        c->setFont(font_);
        c->setFontColor(color_);
        c->drawString(l1, CRect(vs.left, vs.top, vs.right, vs.top + 16), kLeftText);
        c->drawString(l2, CRect(vs.left, vs.top + 18, vs.right, vs.top + 34), kLeftText);
        setDirty(false);
    }

private:
    const stunedrev::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor color_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
```

- [ ] **Step 4: `stunedrev_processor.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// The APF of the score: four independent lines of 42 all-pass sections in
// series, one per face of STONED, each tuned by an irrational ratio. A long
// memory more than a reverberation: the energy of a line returns on average
// after the sum of its 42 delays (106, 69, 17, 201 s at the starting times).
//
// FAUST REFERENCE (seam.tedesco.lib, seam.moorer.lib, seam.math.lib):
//
//   apfv(md,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(md,t-1),_)~(0-_) : mem+_;
//   ms2npsamp(ms)  = select2(n < 2, n : sff.np, n)
//                    with { n = int(floor(ms*ma.SR/1000 + 0.5)); };
//   stdel(k,i,ms)  = sma.ms2npsamp(ms*(i+1)*k);
//   stmd(k,i)      = int(100*(i+1)*k*ma.SR/1000) + 150;
//   stline(k,ms)   = seq(i, 42, sjm.apfv(stmd(k,i), stdel(k,i,ms), 1/sqrt(2)));
//   stunedrev(t1,t2,t3,t4) = stline(sqrt(2),t1), stline((1+sqrt(5))/2,t2),
//                            stline(ma.E,t3), stline(ma.PI,t4);
//
// Re-implemented by hand (seam-ltm convention) in stunedrev_dsp.h, on the
// reusable libraries of _common: seam_moorer.h (sjm.apfv), seam_primes.h (a
// sieve: sff.np without division on the audio thread) and seam_ramp.h. Each section is sized exactly for its longest delay in one
// arena allocated in setActive: stmd's +150 is Faust's compile-time margin.
//
// SR rule of the SSCDO#2 port: times are milliseconds at the session's
// rate, so every memory is the 96 kHz one; each rate has its own primes.
//
// What the plugin adds to the spec: input and output gains (CC83, CC84),
// POWER, 25 ms ramps, RESET, and the centroid readout.
//
// Studies and decisions: doc/study/sscdo2/ (stunedrev-*), logs/2026-10-01-sscdo2-plugins.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "stunedrev_params.h"

namespace Seam {

class StunedrevProcessor : public Steinberg::Vst::SingleComponentEffect,
                           public VSTGUI::VST3EditorDelegate {
public:
    StunedrevProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new StunedrevProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* in, Steinberg::int32 numIn,
        Steinberg::Vst::SpeakerArrangement* out, Steinberg::int32 numOut) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    // VST3EditorDelegate: the RESET square and the footer.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }

    stunedrev::Engine   engine_;
    stunedrev::ParamBox box_;
};

} // namespace Seam
```

The double inheritance is dslar's (`DSLARProcessor : SingleComponentEffect, VST3EditorDelegate`), which declares no extra macros: do the same.

- [ ] **Step 5: `stunedrev_processor.cpp`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · stunedrev — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "stunedrev_processor.h"
#include "stunedrev_ids.h"
#include "stunedrev_state.h"
#include "stunedrev_views.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(stunedrev::Param p) { return kParamPower + (ParamID)p; }

tresult PLUGIN_API StunedrevProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four lines, one per face of STONED, in the original's order:
    // sqrt(2), phi, e, pi on channels 1-4.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    using namespace stunedrev;
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    const char16* names[kLines] = { STR16("t sqrt2"), STR16("t phi"), STR16("t e"), STR16("t pi") };
    for (int j = 0; j < kLines; ++j) {
        auto* t = new RangeParameter(names[j], kParamT1 + j, STR16("ms"),
            kTMin, kTMax, kDefaultTimes[j], kTimeSteps, ParameterInfo::kCanAutomate);
        t->setPrecision(0);
        parameters.addParameter(t);
    }

    auto* in = new RangeParameter(STR16("Input"), kParamInput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    in->setPrecision(3);
    parameters.addParameter(in);

    auto* out = new RangeParameter(STR16("Output"), kParamOutput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    out->setPrecision(3);
    parameters.addParameter(out);

    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

// The arena is allocated and zeroed here, never in process(): 588 MiB at
// 96 kHz, 1.2 GiB at 192 kHz. A failed allocation leaves the plugin silent
// and the footer says so; the host keeps running.
tresult PLUGIN_API StunedrevProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        stunedrev::applyTo(box_.plain(), engine_);   // recalled values become the targets...
        engine_.reset();                             // ...and the ramps start on them
    } else {
        engine_.release();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API StunedrevProcessor::process(ProcessData& data) {
    if (data.inputParameterChanges) {
        const int32 nq = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < nq; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            const int32 np = q->getPointCount();
            if (np <= 0) continue;
            // The last point of the block, applied at its start (suite convention).
            int32 off; ParamValue v;
            const ParamID id = q->getParameterId();
            if (id < kParamPower || id > kParamOutput) continue;
            if (q->getPoint(np - 1, off, v) == kResultOk)
                box_.store((stunedrev::Param)(id - kParamPower), v);
        }
    }
    stunedrev::applyTo(box_.plain(), engine_);

    if (data.numOutputs > 0 && data.numSamples > 0) {
        // A memory of minutes is never silent by inheritance.
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= stunedrev::kLines &&
            data.outputs[0].numChannels >= stunedrev::kLines;
        if (!shaped) {
            const uint32 bytes = getSampleFramesSizeInBytes(processSetup, data.numSamples);
            for (int32 c = 0; c < data.outputs[0].numChannels; ++c) if (out[c]) memset(out[c], 0, bytes);
        } else {
            void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
            if (data.symbolicSampleSize == kSample32)
                engine_.process(reinterpret_cast<float**>(in), reinterpret_cast<float**>(out), data.numSamples);
            else
                engine_.process(reinterpret_cast<double**>(in), reinterpret_cast<double**>(out), data.numSamples);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

// setState runs on the UI thread while process() may run: the values go to
// the box (no order matters), and the editor follows.
tresult PLUGIN_API StunedrevProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    stunedrev::readState(state, box_);
    for (int i = 0; i < stunedrev::kNumParams; ++i)
        setParamNormalized(idOf((stunedrev::Param)i), box_.normalized((stunedrev::Param)i));
    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    stunedrev::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API StunedrevProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API StunedrevProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "stunedrev.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API StunedrevProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (!name) return nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor, frame = VSTGUI::kGreyCColor,
                   idle = VSTGUI::kBlackCColor, azure(0x4a, 0x9e, 0xc8, 0xff);
    if (description) {
        description->getColor("TextLight", text);
        description->getColor("Structure", frame);
        description->getColor("BgDark", idle);
        description->getColor("SliderActive", azure);
    }
    if (std::string(name) == "StunedrevReset")
        return new StunedrevResetButton(VSTGUI::CRect(0, 0, 14, 14), &engine_, frame, idle, azure);
    if (std::string(name) == "StunedrevFooter") {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        if (!font) font = VSTGUI::kNormalFontSmall;
        return new StunedrevFooter(VSTGUI::CRect(0, 0, 400, 36), &engine_, font, text);
    }
    return nullptr;
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::StunedrevProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM STUNEDREV",
        0,
        "Fx|Reverb",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::StunedrevProcessor::createInstance)
END_FACTORY
```

`Engine::process` takes `const T* const*`; `float**` converts to it implicitly.

- [ ] **Step 6: `resource/stunedrev.uidesc`** (format L, 460 × 500; columns at x = 30 and 250, width 180; FINE blocks on a 58 px stride):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<vstgui-ui-description version="1">
    <fonts>
        <font font-name="Source Code Pro Light" name="TitleFont" size="20"/>
        <font font-name="Source Code Pro Light" name="SubtitleFont" size="13"/>
        <font font-name="Source Code Pro Light" name="KnobLabelFont" size="13"/>
        <font font-name="Source Code Pro Light" name="ValueFont" size="12"/>
        <font font-name="Source Code Pro Light" name="InfoFont" size="12"/>
    </fonts>
    <colors>
        <color name="BgDark" rgba="#292c2fff"/>
        <color name="TextLight" rgba="#fcfbfdff"/>
        <color name="SliderTrack" rgba="#444444ff"/>
        <color name="SliderActive" rgba="#4a9ec8ff"/>
        <color name="Structure" rgba="#888888ff"/>
    </colors>

    <!-- L format: six fine controls, two columns of 180 px at x=30 and
         x=250. Left: the four times, in the lines' order. Right: input and
         output. No SETUP zone. Zone order HEADER, OPS, FINE, FOOTER. -->
    <template name="view" class="CViewContainer" origin="0, 0" size="460, 500"
              minSize="460, 500" maxSize="460, 500"
              background-color="BgDark" background-color-draw-style="filled">

        <!-- ── HEADER ─────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="0, 14" size="460, 26" font="TitleFont"
              font-color="TextLight" text-alignment="center" title="SEAM STUNEDREV" transparent="true"/>
        <view class="CTextLabel" origin="0, 42" size="460, 18" font="SubtitleFont"
              font-color="TextLight" text-alignment="center" title="Studio sul Corpo d'Ombra #2" transparent="true"/>
        <view class="CTextLabel" origin="0, 60" size="460, 14" font="InfoFont"
              font-color="TextLight" text-alignment="center"
              title="four tuned all-pass memories &#xB7; &#x221A;2 &#x3C6; e &#x3C0;" transparent="true"/>

        <!-- ── OPS ───────────────────────────────────────────────────────
             POWER over the left column, RESET over the right one. RESET
             is UI-only: it empties the memory (about 0.4 s), not a parameter. -->
        <view class="CCheckBox" origin="93, 90" size="14, 14" control-tag="Power"
              boxframe-color="Structure" boxfill-color="BgDark" checkmark-color="SliderActive"
              title="" transparent="true"/>
        <view class="CTextLabel" origin="111, 88" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="POWER" transparent="true"/>
        <view class="CView" origin="313, 90" size="14, 14" custom-view-name="StunedrevReset"
              tooltip="empty the four memories"/>
        <view class="CTextLabel" origin="331, 88" size="52, 16" font="KnobLabelFont"
              font-color="TextLight" text-alignment="left" title="RESET" transparent="true"/>

        <!-- ── FINE ─────────────────────────────────────────────────────── -->
        <view class="CTextLabel" origin="30, 120" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="t &#x221A;2 (ms)" transparent="true"/>
        <view class="CSlider" origin="30, 136" size="180, 18" control-tag="T1"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 156" size="180, 16" font="ValueFont" control-tag="T1"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 178" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="t &#x3C6; (ms)" transparent="true"/>
        <view class="CSlider" origin="30, 194" size="180, 18" control-tag="T2"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 214" size="180, 16" font="ValueFont" control-tag="T2"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 236" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="t e (ms)" transparent="true"/>
        <view class="CSlider" origin="30, 252" size="180, 18" control-tag="T3"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 272" size="180, 16" font="ValueFont" control-tag="T3"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="30, 294" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="t &#x3C0; (ms)" transparent="true"/>
        <view class="CSlider" origin="30, 310" size="180, 18" control-tag="T4"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="30, 330" size="180, 16" font="ValueFont" control-tag="T4"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="0" style-no-frame="true"/>

        <view class="CTextLabel" origin="250, 120" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="input" transparent="true"/>
        <view class="CSlider" origin="250, 136" size="180, 18" control-tag="Input"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="250, 156" size="180, 16" font="ValueFont" control-tag="Input"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="3" style-no-frame="true"/>

        <view class="CTextLabel" origin="250, 178" size="180, 14" font="KnobLabelFont"
              font-color="TextLight" text-alignment="center" title="output" transparent="true"/>
        <view class="CSlider" origin="250, 194" size="180, 18" control-tag="Output"
              orientation="horizontal" draw-back="true" draw-back-color="SliderTrack"
              draw-value="true" draw-value-color="SliderActive"
              draw-frame="true" draw-frame-color="SliderTrack"
              frame-width="1" mode="free click" transparent="false"/>
        <view class="CTextEdit" origin="250, 214" size="180, 16" font="ValueFont" control-tag="Output"
              font-color="TextLight" text-alignment="center" transparent="true"
              value-precision="3" style-no-frame="true"/>

        <!-- ── FOOTER — what the plugin reports, then the logo ──────────
             The energy centroid of each line (how long each face
             remembers), the arena, the status. -->
        <view class="CView" origin="30, 366" size="400, 36" custom-view-name="StunedrevFooter"/>

        <!-- SEAM logo (native 240x77 — CView does not scale bitmaps) -->
        <view class="CView" origin="110, 412" size="240, 77" bitmap="logo"/>
    </template>

    <bitmaps><bitmap name="logo" path="seam_logo.png"/></bitmaps>
    <control-tags>
        <control-tag name="Power"  tag="100"/>
        <control-tag name="T1"     tag="101"/>
        <control-tag name="T2"     tag="102"/>
        <control-tag name="T3"     tag="103"/>
        <control-tag name="T4"     tag="104"/>
        <control-tag name="Input"  tag="105"/>
        <control-tag name="Output" tag="106"/>
    </control-tags>
</vstgui-ui-description>
```

- [ ] **Step 7: `plugins/stunedrev/CMakeLists.txt`** (LMO's with the names changed):

```cmake
cmake_minimum_required(VERSION 3.25.0)

project(seam-stunedrev
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM STUNEDREV – SSCDO#2 tuned all-pass memories"
)

set(stunedrev_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/stunedrev_ids.h
    source/stunedrev_dsp.h
    source/stunedrev_params.h
    source/stunedrev_state.h
    source/stunedrev_views.h
    source/stunedrev_processor.cpp
    source/stunedrev_processor.h
    source/version.h
    resource/stunedrev.uidesc
)

set(target stunedrev)

smtg_add_vst3plugin(${target} ${stunedrev_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/stunedrev.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.stunedrev"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
```

Add `    add_subdirectory(plugins/stunedrev)` after `    add_subdirectory(plugins/lmo)` in the root `CMakeLists.txt`.

- [ ] **Step 8: Build.** Run: `cmake -S . -B build && cmake --build build --config Release --target stunedrev 2>&1 | tail -5`
Expected: `** BUILD SUCCEEDED **`. Compile errors in the views are VSTGUI API names: check them against `vst3sdk/vstgui4/vstgui/lib/` (`cvstguitimer.h`, `cdrawcontext.h`) and against `dslar_reset_button.h`, which builds today.

- [ ] **Step 9: Validator.** Run: `build/bin/Release/validator build/VST3/Release/stunedrev.vst3 2>&1 | tail -5`
Expected: all tests passed (47 of 47, as LMO). The validator activates the plugin at several rates: each `setActive(true)` allocates the arena, so a slow run is expected.

- [ ] **Step 10: Lint and the whole suite.** Run: `python3 tools/check-uidesc.py && cmake --build build-test --config Release && ctest --test-dir build-test -C Release 2>&1 | tail -5`
Expected: lint 0 errors (two WARNs for the missing screenshot and gallery entry are expected until Task 9); ctest all passed.

- [ ] **Step 11: Commit.**

```bash
git add plugins/stunedrev CMakeLists.txt
git commit -m "feat(stunedrev): the processor, RESET and footer views, format-L window"
```

---

### Task 8: Mutations and CPU

**Files:**
- Create: `doc/study/sscdo2/stunedrev-plugin/mutations.md`
- Create: `doc/study/sscdo2/stunedrev-plugin/cpu.cpp`

- [ ] **Step 1: Apply each mutation, rebuild the named test, record RED or GREEN, restore the source** (`git diff` empty after each). The mutations:

| test | mutation |
|---|---|
| seam_primes_test | `nextPrimeAbove` starts at `n` when n is odd (not strictly greater) |
| seam_primes_test | sieve inner step `p` instead of `2 * p` |
| stunedrev_dsp_test: delays | `std::floor(ms * fs / 1000.0)` without `+ 0.5` |
| stunedrev_dsp_test: delays | φ written `1.618` |
| seam_moorer_test | `+g_` instead of `-g_` |
| seam_moorer_test | read `t_` back instead of `t_ - 1` |
| seam_moorer_test: clear | `clear()` keeps `v_` |
| stunedrev_dsp_test: arena | `sectionLength` without `+ 1` |
| stunedrev_dsp_test: engine 96/48 | ratios e and π swapped |
| stunedrev_dsp_test: engine 96/48 | the loop over sections stops at 41 |
| stunedrev_dsp_test: change of time | `setTime` stores the time but `applyTime` runs only in `prepare` |
| stunedrev_dsp_test: in-place | outputs written inside the input loop (`out[j][k]` before all `in[j][k]` are read) |
| stunedrev_dsp_test: engine 96/48 | `setGain(kG)` omitted in `prepare` (g = 0) |
| stunedrev_dsp_test: RESET fresh | `clearPos_ += m + want` (a chunk skipped) |
| stunedrev_dsp_test: RESET fresh | `s.clear()` not called at the end of the clearing |
| stunedrev_dsp_test: RESET twice | `if (gen != servedGen_ && phase_ == Phase::Run)` |
| stunedrev_dsp_test: RESET silent | lines keep running during `Phase::Clear` |
| stunedrev_params_test: off-grid | `std::lround(norm * kTimeSteps)` |
| stunedrev_state_test | `readState` does not store the fields it read |

Write `mutations.md` in the format of `doc/study/sscdo2/lmo-plugin/mutations.md`: a title, two sentences on the method, the table with a result column. A mutation that stays GREEN is either an equivalent mutant (explain why, as LMO did for commuting sections) or a hole: in that case, write the missing test in the owning task's test file, see it RED, and record both.

- [ ] **Step 2: CPU.** `doc/study/sscdo2/stunedrev-plugin/cpu.cpp`:

```cpp
// cpu.cpp -- the cost of the stunedrev engine: seconds of audio per second of
// CPU at 96 and 192 kHz, 256-sample blocks, the burst then silence.
// c++ -std=c++17 -O3 -I ../../../../plugins/stunedrev/source -I ../../../../plugins/_common -I ../../../../tests cpu.cpp -o cpu
#include "stunedrev_dsp.h"
#include "stunedrev_burst.h"
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    for (double fs : {96000.0, 192000.0}) {
        stunedrev::Engine e;
        if (!e.prepare(fs)) { printf("allocation failed at %g\n", fs); continue; }
        e.setInput(1.0); e.setOutput(1.0); e.setPower(true); e.reset();
        const int B = 256; const double seconds = 20.0;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
        Burst burst(fs);
        const long blocks = (long)(seconds * fs / B);
        const auto t0 = std::chrono::steady_clock::now();
        for (long b = 0; b < blocks; ++b) { burst.fill(in, B); e.process(in, out, B); }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("%6.0f Hz: %.1f %% of a core (%.2f s of CPU for %.0f s), arena %.0f MiB\n",
               fs, 100.0 * cpu / seconds, cpu, seconds, e.arenaBytes() / 1048576.0);
    }
}
```

Run it from its folder with the compile line in its header. Record the output in the plugin README (Task 9) and in `doc/study/sscdo2/stunedrev-plugin/README.md`.

- [ ] **Step 3: Commit.**

```bash
git add doc/study/sscdo2/stunedrev-plugin/mutations.md doc/study/sscdo2/stunedrev-plugin/cpu.cpp
git commit -m "test(stunedrev): every test verified by mutation; CPU measured"
```

---

### Task 9: Documentation, registry, report, host check

**Files:**
- Create: `plugins/stunedrev/doc/README.md`, `doc/study/sscdo2/stunedrev-plugin/README.md`
- Modify: `doc/study/sscdo2/README.md` (table row), `doc/plugins.toml`, `doc/scripts/test-doc.sh:21,35,38`, `doc/scripts/render-readme.py:31`, `CLAUDE.md:90,92`, `logs/2026-10-01-sscdo2-plugins.md`, `doc/study/sscdo2/report/parte2-stunedrev.tex`
- Create (from Giuseppe): `docs/img/stunedrev.png`

- [ ] **Step 1: `plugins/stunedrev/doc/README.md`**, on the model of `plugins/lmo/doc/README.md`, one sentence per line:
  - what stunedrev is (the APF, four faces, a memory more than a reverberation; outputs 1–4 = √2, φ, e, π);
  - the parameter table of the spec;
  - the time jump (a click, never garbage; times are settings, no cue moves them);
  - RESET (what it does, the 0.39 s, not a parameter and not saved; why);
  - the sample-rate reading (milliseconds at the session's rate, primes per rate, centroids within a prime gap per section of 96 kHz);
  - the memory (exact sizing against `stmd`'s +150, the measured arena at 96 kHz from Task 3, 1.2 GiB at 192 kHz, allocation failure);
  - the specification block (the Faust of the processor header) and the verification (the measured relative errors of Task 4, the mutations, the CPU of Task 8);
  - out of scope (the direct `adc~` question for Davide; no crossfade on a time change; cues and MIDI in Reaper).

- [ ] **Step 2: `doc/study/sscdo2/stunedrev-plugin/README.md`** (scope README): each file (`gen-ref.sh`, `refdump.cpp`, `dsp/*.dsp`, `cpu.cpp`, `mutations.md`, and `tests/stunedrev_burst.h`), and the two libraries it added to `_common/` (`seam_primes.h`, `seam_moorer.h`), how to run (`FAUSTLIBS=... ./gen-ref.sh`, the ctest line, the cpu compile line), how it fits (references → `tests/ref/stunedrev_ref.h` → `stunedrev_dsp_test`), the measured results. Add to `doc/study/sscdo2/README.md`'s table, after the LMO plugin row:
  `| stunedrev | the C++ plugin | stunedrev-plugin/ | plugins/stunedrev, equal to sdt.stunedrev within <measured> of the peak | none |`

- [ ] **Step 3: Registry.** Append to the "Works — SSCDO#2" family in `doc/plugins.toml`, after LMO:

```toml
  [[family.plugin]]
  name = "STUNEDREV"
  io = "4ch → 4ch"
  screenshot = "stunedrev.png"
  faust = "seam.tedesco.lib"
  description = """
The APF of Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco): four independent lines of 42 Moorer all-pass sections in series, one per face of STONED, tuned by √2, φ, e and π. Each delay is a time in milliseconds moved to the next prime at the session's rate, so each face returns the sound on its own time scale, from seconds to minutes. One arena sized exactly at activation, and a RESET that empties it while playing"""
```

Change the counts from 17 to 18 in `doc/scripts/test-doc.sh` (lines 21, 35, 38: the number and the message), "seventeen" to "eighteen" in `doc/scripts/render-readme.py:31` and `CLAUDE.md:90,92`.
Run: `make -C doc doc && make -C doc test`
Expected: README regenerated with the new row and gallery entry; 12 checks ok, except the screenshot copy check, which needs `docs/img/stunedrev.png` (Step 6).

- [ ] **Step 4: Log.** Append to `logs/2026-10-01-sscdo2-plugins.md` a section `## stunedrev` with: the decisions of the brainstorming (input/output in the plugin, the time jump as the spec, RESET in OPS as a GUI-only generation counter, POWER added, centroid in the footer, arena unique and exactly sized); the deviations found during implementation (for instance the RESET rate as bytes per sample instead of 4 MiB per block; the noise burst in place of the clarinet for test 4, the clarinet staying for the listening; any product-order adjustment of Task 3); the verification numbers; the mutations; the CPU. Replace the line `- stunedrev.` under `## Open` with the choir.

- [ ] **Step 5: Report.** In `doc/study/sscdo2/report/parte2-stunedrev.tex`:
  - card `stunedrev-memoria`: the plugin's arena is the number measured in Task 3, with the reason (exact sizing); keep the `\misura` mechanism (add the number to `doc/study/sscdo2/stunedrev-plugin/README.md` so that `check.py` finds it with the source `stunedrev-plugin`; read `report/check.py` to see how sources map to folders);
  - a new card `stunedrev-reset` (state `\deciso`): POWER and RESET as additions against the original, what RESET does and that it is not in the presets.
  Run: `make -C doc/study/sscdo2/report check`
  Expected: build ok, every `\misura` verified. Commit the sources with the rebuilt PDF.

- [ ] **Step 6: Commit, then hand over to Giuseppe** for the host check, which no test replaces:

```bash
git add plugins/stunedrev/doc doc logs CLAUDE.md
git commit -m "docs(stunedrev): plugin README, study, registry (eighteen plugins), log, report"
```

Ask Giuseppe to: load stunedrev in Reaper at 96 kHz (4-channel track); play `doc/study/sscdo2/delrm-comb/renders/ccb_dry.wav` through it with input and output at 1 and compare by ear with `stunedrev-chain/renders/` (regenerate with `render.py`); try POWER, RESET while sounding, a time moved by hand, the same session at 48 kHz; send the screenshot of the window for `docs/img/stunedrev.png`.
With the screenshot: `make -C doc doc && make -C doc test` (12 checks ok), `python3 tools/check-uidesc.py` (no WARN), commit.
Then ask before `make -C doc publish` and before any `git push`.
