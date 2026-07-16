# strx — STONE Observation M/S Analyzer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `strx`, a standalone VST3 M/S observation analyzer for STONE (goniometer + M+S spectrum + M/S/Width meters), with its DSP anchored to new Faust `san` analyzers and a reusable FFT.

**Architecture:** One `SingleComponentEffect` object with stereo pass-through. The audio thread runs an SDK-free analysis core (`strx_dsp.h`) that writes an `AnalysisFrame` into a lock-free triple-buffer; three custom `CView`s read the latest frame on a GUI timer. Faust is the spec: new `san.correlation/width/panorama` in `seam.analyzers.lib` and a reusable `_common/seam_fft.h` are ported by hand into the core.

**Tech Stack:** C++17, VST3 SDK (`SingleComponentEffect`), VSTGUI (`CGraphicsPath`, `VST3EditorDelegate::createCustomView`), doctest, Faust (spec verification only).

## Global Constraints

- Design spec: `docs/superpowers/specs/2026-07-16-strx-observation-analyzer-design.md` (authority for every decision below).
- Project rule: Faust is the spec, hand-written readable C++ is the deliverable. No `faust -lang cpp` in `plugins/*/source/`. Every DSP header opens with a `// FAUST REFERENCE (seam.<lib>.lib): ...` block.
- Build: CMake at repo root, `-G Xcode`. VST3 SDK at `-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`.
- Plugin layout is canonical (`plugins/<name>/{CMakeLists.txt,source/,resource/,doc/}`); register with `add_subdirectory(plugins/strx)` in the root `CMakeLists.txt`.
- DSP cores are header-only, SDK-free, unit-tested with doctest under `tests/`, one `add_executable` + `add_test` per core in `tests/CMakeLists.txt`.
- `VSTGUI_LIVE_EDITING=0` (the suite ships finished GUIs).
- Language: code/commits/docs in English.
- Commit trailers on every commit:
  ```
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y
  ```
- Reference (GS's own): `/Users/giuseppe/Documents/gitlab/gs/the-accountant/src/analysis/fft.hpp` — a 54-line radix-2 Cooley-Tukey DIT FFT to adapt (not copy). The VST3 SDK has no public FFT.

## File Structure

Created / modified across the plan:

- `librerie/faust-libraries/src/seam.analyzers.lib` — **modify**: add `san.correlation`, `san.width`, `san.panorama`, `san.vectorangle` (numeric, no GUI, each with inline test).
- `plugins/_common/seam_fft.h` — **create**: SDK-free real radix-2 FFT + Hann window + Welch power-spectrum accumulator.
- `tests/seam_fft_test.cpp`, `tests/strx_dsp_test.cpp` — **create**: doctest cores; register in `tests/CMakeLists.txt` (**modify**).
- `plugins/strx/source/strx_ids.h` — **create**: plugin UID, processor/controller class IDs, custom-view name tags. No automatable parameter IDs.
- `plugins/strx/source/strx_dsp.h` — **create**: `AnalysisFrame` + `Seam::strx::Analyzer` (M/S, followers, correlation/width/panorama, Welch M&S, goniometer decimation, triple-buffer).
- `plugins/strx/source/strx_processor.h` / `.cpp` — **create**: `SingleComponentEffect` + `VST3EditorDelegate`; stereo pass-through; `createCustomView`.
- `plugins/strx/source/strx_goniometer.h`, `strx_spectrum.h`, `strx_meters.h` — **create**: three custom `CView`s.
- `plugins/strx/source/version.h` — **create**.
- `plugins/strx/resource/strx.uidesc` — **create**: three-zone layout + custom-view tags.
- `plugins/strx/CMakeLists.txt` — **create**; register in root `CMakeLists.txt` (**modify**).
- `plugins/strx/doc/` — **create**: short README linking the spec and the Faust citations.

Pattern sources to read before writing boilerplate: `plugins/dslar/source/dslar_processor.{h,cpp}` (SingleComponentEffect + delegate), `plugins/dslar/source/dslar_reset_button.h` (custom CView + createCustomView), `plugins/dslar/CMakeLists.txt`, `plugins/dslar/resource/dslar.uidesc`, `tests/seam_meter_test.cpp` (doctest core test), `plugins/_common/seam_meter.h` (reused math).

---

### Task 1: Faust analyzers — `san.correlation`, `san.width`, `san.panorama`, `san.vectorangle`

New numeric analyzers in the SEAM Faust library, the *spec* the C++ core ports. Verified by compiling their inline `process` tests with `faust`. No doctest (this task is Faust-only).

**Files:**
- Modify: `librerie/faust-libraries/src/seam.analyzers.lib` (append a "STEREO FIELD ANALYZERS" section before the closing lines).

**Interfaces:**
- Consumes: nothing.
- Produces (Faust functions, all `(l, r) -> _`, referenced by name in later `FAUST REFERENCE` blocks):
  - `san.correlation(l, r)` — normalized L/R cross-correlation in [-1, 1].
  - `san.width(l, r)` — Side/Mid RMS ratio in [0, 1] (0 = mono).
  - `san.panorama(l, r)` — L/R balance in [-1, 1] (0 = centered).
  - `san.vectorangle(l, r)` — principal-axis angle of the L/R covariance, radians.

- [ ] **Step 1: Read the library header and existing style**

Read `librerie/faust-libraries/src/seam.analyzers.lib` (75 lines). Note: it declares `san = library("seam.analyzers.lib");`, imports `seam.lib`, and every function has a commented `// process = ...` inline test. Averaging uses one-pole smoothing; use `si.smooth(ba.tau2pole(tau))` with a fixed `tau` (e.g. 0.3 s) for the moving means.

- [ ] **Step 2: Append the four analyzers with inline tests**

Append to `seam.analyzers.lib` (before any trailing blank lines):

```faust
//---------------------------------------------------- STEREO FIELD ANALYZERS --
// Numeric-only stereo-field descriptors (no GUI). Moving means via one-pole
// smoothing over a fixed window. Ported by hand into seam-ltm strx_dsp.h.
sfa_tau = 0.3; // moving-average window (s)
sfa_avg(x) = x : si.smooth(ba.tau2pole(sfa_tau));
sfa_eps = 1e-12;
//
// Normalized L/R cross-correlation in [-1,1]: mean(l*r)/sqrt(mean(l^2)*mean(r^2)).
correlation(l, r) = sfa_avg(l*r) / sqrt(max(sfa_eps, sfa_avg(l*l) * sfa_avg(r*r)));
// process = os.osc(1000) <: _, _ : correlation;   // correlated -> ~1
//
// Side/Mid RMS ratio in [0,1] (0 = mono). M=(l+r)/sqrt(2), S=(l-r)/sqrt(2).
width(l, r) = rmsS / max(sfa_eps, rmsM + rmsS) with {
    m = (l + r) / sqrt(2);
    s = (l - r) / sqrt(2);
    rmsM = sqrt(sfa_avg(m*m));
    rmsS = sqrt(sfa_avg(s*s));
};
// process = os.osc(1000), os.osc(1000)*(-1) : width;   // anti-phase -> ~1
//
// L/R energy balance in [-1,1] (0 = centered, +1 = hard right).
panorama(l, r) = (er - el) / max(sfa_eps, er + el) with {
    el = sfa_avg(l*l);
    er = sfa_avg(r*r);
};
// process = os.osc(1000), 0 : panorama;   // left only -> ~ -1
//
// Principal-axis angle of the L/R covariance (radians), 0 = mono/vertical.
vectorangle(l, r) = 0.5 * atan2(2*sfa_avg(l*r), sfa_avg(l*l) - sfa_avg(r*r));
// process = os.osc(1000) <: _, _ : vectorangle;   // correlated -> ~pi/4
```

- [ ] **Step 3: Verify each inline test compiles**

Run each (uncomment the `process` line one at a time, or pass inline):

```bash
cd librerie/faust-libraries
echo 'import("src/seam.lib"); process = os.osc(1000) <: _,_ : san.correlation;' | faust -
echo 'import("src/seam.lib"); process = os.osc(1000), os.osc(1000)*(-1) : san.width;' | faust -
echo 'import("src/seam.lib"); process = os.osc(1000), 0 : san.panorama;' | faust -
echo 'import("src/seam.lib"); process = os.osc(1000) <: _,_ : san.vectorangle;' | faust -
```

Expected: each prints C++ to stdout with no diagnostic (exit 0). If `faust` is missing, note it and proceed — the C++ port in Task 4 is the deliverable and carries its own doctest.

- [ ] **Step 4: Commit**

```bash
git add librerie/faust-libraries/src/seam.analyzers.lib
git commit -m "feat(seam.analyzers): stereo-field analyzers (correlation/width/panorama/vectorangle)

New numeric-only san.* descriptors — the Faust spec for strx's M/S analysis.
Fills a gap in official faustlibraries (no stereo correlation/width). Each
carries an inline process test.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

> Note: `seam.analyzers.lib` lives in a **separate git repo** (`librerie/faust-libraries`). Run the git commands from that directory.

---

### Task 2: `seam_fft.h` — real radix-2 FFT core

The reusable FFT primitive (reused by Spec 3). In-place radix-2 Cooley-Tukey DIT, allocation-free after `prepare`.

**Files:**
- Create: `plugins/_common/seam_fft.h`
- Create: `tests/seam_fft_test.cpp`
- Modify: `tests/CMakeLists.txt` (add `seam_fft_test` executable + `add_test`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `void seam::fft::transform(float* data, int n, bool forward)` — in-place radix-2 on `n` interleaved complex pairs (`data` length `2*n`, layout `re,im,re,im,...`); `n` a power of two; no `1/n` scaling; `forward` uses `e^{-i}`.

- [ ] **Step 1: Write the failing test**

Create `tests/seam_fft_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_fft.h"
#include <cmath>
#include <vector>

using seam::fft::transform;

// helper: fill interleaved complex from a real signal
static std::vector<float> cplx(const std::vector<float>& re) {
    std::vector<float> d(re.size() * 2, 0.0f);
    for (size_t i = 0; i < re.size(); ++i) d[2*i] = re[i];
    return d;
}

TEST_CASE("FFT of a DC signal puts all energy in bin 0") {
    std::vector<float> re(8, 1.0f);
    auto d = cplx(re);
    transform(d.data(), 8, true);
    CHECK(d[0] == doctest::Approx(8.0f));       // bin 0 real = sum
    CHECK(d[1] == doctest::Approx(0.0f));
    for (int k = 1; k < 8; ++k)
        CHECK(std::hypot(d[2*k], d[2*k+1]) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("FFT of a full-scale cosine peaks at its bin (k=1)") {
    const int N = 16;
    std::vector<float> re(N);
    for (int n = 0; n < N; ++n) re[n] = std::cos(2.0 * M_PI * 1 * n / N);
    auto d = cplx(re);
    transform(d.data(), N, true);
    CHECK(std::hypot(d[2*1], d[2*1+1]) == doctest::Approx(N/2.0).epsilon(1e-4));
    CHECK(std::hypot(d[2*(N-1)], d[2*(N-1)+1]) == doctest::Approx(N/2.0).epsilon(1e-4));
}

TEST_CASE("forward then inverse (with 1/N) recovers the signal") {
    const int N = 8;
    std::vector<float> re = {1,2,3,4,4,3,2,1};
    auto d = cplx(re);
    transform(d.data(), N, true);
    transform(d.data(), N, false);
    for (int n = 0; n < N; ++n)
        CHECK(d[2*n]/N == doctest::Approx(re[n]).epsilon(1e-4));
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(seam_fft_test
    seam_fft_test.cpp
)
target_include_directories(seam_fft_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_fft_test PRIVATE cxx_std_17)
add_test(NAME seam_fft_test COMMAND seam_fft_test)
```

Run:
```bash
cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target seam_fft_test 2>&1 | tail -5
```
Expected: FAIL — `seam_fft.h` not found / `seam::fft::transform` undefined.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/_common/seam_fft.h` (adapted from the-accountant `fft.hpp`, but operating on a raw interleaved float buffer so it is allocation-free at the call site):

```cpp
// SEAM-LTM · seam_fft — SDK-free radix-2 FFT (shared analysis primitive).
//
// In-place Cooley-Tukey radix-2 DIT on interleaved complex pairs. Adapted from
// GS's the-accountant/src/analysis/fft.hpp. Reused by strx (Welch spectrum) and,
// later, the STONE transfer-function measurement (Spec 3).
#pragma once
#include <cmath>

namespace seam { namespace fft {

// data: n interleaved complex pairs (re,im,...), length 2*n. n must be a power
// of two. No 1/n scaling. forward=true → e^{-i2pi kn/N}.
inline void transform(float* data, int n, bool forward) {
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = data[2*i],   ti = data[2*i+1];
            data[2*i]   = data[2*j];   data[2*i+1] = data[2*j+1];
            data[2*j]   = tr;          data[2*j+1] = ti;
        }
    }
    const float sign = forward ? -1.0f : 1.0f;
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = sign * 2.0f * float(M_PI) / len;
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;              // running twiddle
            for (int k = 0; k < len/2; ++k) {
                const int a = 2*(i+k), b = 2*(i+k+len/2);
                const float ur = data[a],   ui = data[a+1];
                const float vr = data[b]*cr - data[b+1]*ci;
                const float vi = data[b]*ci + data[b+1]*cr;
                data[a]   = ur + vr;  data[a+1] = ui + vi;
                data[b]   = ur - vr;  data[b+1] = ui - vi;
                const float ncr = cr*wr - ci*wi;     // advance twiddle
                ci = cr*wi + ci*wr;  cr = ncr;
            }
        }
    }
}

}} // namespace seam::fft
```

- [ ] **Step 4: Run the tests and verify they pass**

```bash
cmake --build build --target seam_fft_test 2>&1 | tail -3
ctest --test-dir build -R seam_fft_test --output-on-failure
```
Expected: PASS (3 test cases).

- [ ] **Step 5: Commit**

```bash
git add plugins/_common/seam_fft.h tests/seam_fft_test.cpp tests/CMakeLists.txt
git commit -m "feat(_common): seam_fft.h radix-2 FFT core + doctest

Adapted from the-accountant fft.hpp; interleaved-complex, allocation-free.
Shared primitive, reused by Spec 3.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 2b: `seam_fft.h` — Hann window + Welch power-spectrum accumulator

Streaming Welch magnitude with a Hann window, 50% overlap, and a live exponential (EMA) time average. Allocation-free after `prepare`.

**Files:**
- Modify: `plugins/_common/seam_fft.h` (add `seam::fft::Welch`)
- Modify: `tests/seam_fft_test.cpp` (add Welch cases)

**Interfaces:**
- Consumes: `seam::fft::transform`.
- Produces:
  - `class seam::fft::Welch` with:
    - `void prepare(int fftSize, double emaTau, double fs)` — `fftSize` power of two; Hann; 50% overlap.
    - `void reset()`
    - `void push(float x)` — feed one sample; runs an FFT internally each hop.
    - `bool hasNewFrame()` — true once (and clears) when a hop completed.
    - `const float* magnitudeDb() const` — `fftSize/2+1` EMA-averaged magnitudes in dB.
    - `int numBins() const` — `fftSize/2+1`.

- [ ] **Step 1: Write the failing test**

Append to `tests/seam_fft_test.cpp`:

```cpp
#include <cstdio>
TEST_CASE("Welch: a steady sine peaks at the expected bin") {
    const int N = 1024; const double fs = 48000.0;
    seam::fft::Welch w;
    w.prepare(N, 0.05, fs);
    const double f = fs * 64 / N;                 // exactly bin 64
    for (int i = 0; i < N*8; ++i)
        w.push((float)std::sin(2.0*M_PI*f*i/fs));
    const float* mag = w.magnitudeDb();
    int peak = 0;
    for (int k = 1; k < w.numBins(); ++k) if (mag[k] > mag[peak]) peak = k;
    CHECK(peak == 64);
}

TEST_CASE("Welch: silence sits at/below the dB floor") {
    seam::fft::Welch w;
    w.prepare(256, 0.05, 48000.0);
    for (int i = 0; i < 256*8; ++i) w.push(0.0f);
    const float* mag = w.magnitudeDb();
    for (int k = 0; k < w.numBins(); ++k) CHECK(mag[k] <= -90.0f);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target seam_fft_test 2>&1 | tail -5
```
Expected: FAIL — `seam::fft::Welch` undefined.

- [ ] **Step 3: Write minimal implementation**

Add inside `namespace seam { namespace fft {` in `seam_fft.h`, after `transform`:

```cpp
#include <vector>   // (add to the includes at the top of the file)

class Welch {
public:
    void prepare(int fftSize, double emaTau, double fs) {
        n_ = fftSize; bins_ = fftSize/2 + 1; hop_ = fftSize/2;
        fs_ = (fs > 0.0) ? fs : 48000.0;
        // EMA coefficient over the hop rate (one hop = hop_ samples).
        const double dt = double(hop_) / fs_;
        ema_ = (emaTau > 0.0) ? std::exp(-dt / emaTau) : 0.0;
        win_.assign(n_, 0.0f);
        double wsum = 0.0;
        for (int i = 0; i < n_; ++i) {                 // Hann
            win_[i] = 0.5f * (1.0f - std::cos(2.0*M_PI*i/(n_-1)));
            wsum += win_[i]*win_[i];
        }
        winNorm_ = 1.0 / (wsum > 0.0 ? wsum : 1.0);    // power normalization
        ring_.assign(n_, 0.0f);
        scratch_.assign(2*n_, 0.0f);
        magDb_.assign(bins_, -120.0f);
        magLin_.assign(bins_, 0.0f);
        reset();
    }
    void reset() {
        pos_ = 0; sinceHop_ = 0; primed_ = false; newFrame_ = false;
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        std::fill(magLin_.begin(), magLin_.end(), 0.0f);
        std::fill(magDb_.begin(), magDb_.end(), -120.0f);
    }
    void push(float x) {
        ring_[pos_] = x;
        pos_ = (pos_ + 1) % n_;
        if (++sinceHop_ >= hop_) { sinceHop_ = 0; runFrame(); }
    }
    bool hasNewFrame() { bool f = newFrame_; newFrame_ = false; return f; }
    const float* magnitudeDb() const { return magDb_.data(); }
    int numBins() const { return bins_; }
private:
    void runFrame() {
        // windowed frame from the ring (oldest sample = current pos_)
        for (int i = 0; i < n_; ++i) {
            const float s = ring_[(pos_ + i) % n_];
            scratch_[2*i]   = s * win_[i];
            scratch_[2*i+1] = 0.0f;
        }
        transform(scratch_.data(), n_, true);
        for (int k = 0; k < bins_; ++k) {
            const double re = scratch_[2*k], im = scratch_[2*k+1];
            const double p = (re*re + im*im) * winNorm_;   // power
            magLin_[k] = float(p + ema_ * (magLin_[k] - p)); // EMA on power
            const double db = 10.0 * std::log10(magLin_[k] > 1e-12 ? magLin_[k] : 1e-12);
            magDb_[k] = float(db < -120.0 ? -120.0 : db);
        }
        newFrame_ = true;
    }
    int n_=0, bins_=0, hop_=0, pos_=0, sinceHop_=0;
    double fs_=48000.0, ema_=0.0, winNorm_=1.0;
    bool primed_=false, newFrame_=false;
    std::vector<float> win_, ring_, scratch_, magLin_, magDb_;
};
```

Add `#include <algorithm>` and `#include <vector>` to the top includes of `seam_fft.h`.

- [ ] **Step 4: Run the tests and verify they pass**

```bash
cmake --build build --target seam_fft_test 2>&1 | tail -3
ctest --test-dir build -R seam_fft_test --output-on-failure
```
Expected: PASS (5 test cases).

- [ ] **Step 5: Commit**

```bash
git add plugins/_common/seam_fft.h tests/seam_fft_test.cpp
git commit -m "feat(_common): seam_fft Welch accumulator (Hann, 50% overlap, EMA)

Streaming power spectrum in dB, allocation-free after prepare. Verified with
sine-peak and silence-floor doctests.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 3: `strx_dsp.h` — M/S, levels, and stereo-field scalars

The first slice of the analysis core: `AnalysisFrame` (scalars only for now) and the scalar analysis ported from Task 1's `san.*`. Goniometer arrays and spectra are added in Task 4; triple-buffer in Task 5.

**Files:**
- Create: `plugins/strx/source/strx_dsp.h`
- Create: `tests/strx_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt` (add `strx_dsp_test`)

**Interfaces:**
- Consumes: `seam::meter::LevelFollower` (`seam_meter.h`).
- Produces:
  - `struct Seam::strx::AnalysisFrame` with fields (this task adds the scalars; later tasks add arrays):
    - `float inL, inR, mid, side;` — RMS levels, dBFS (floored −60).
    - `float correlation;` — [-1,1].
    - `float width;` — [0,1] (Side/Mid ratio; 0 = mono).
    - `float panorama;` — [-1,1].
    - `float angleRad;` — principal-axis angle, radians.
  - `class Seam::strx::Analyzer` with `void prepare(double fs)`, `void reset()`, `void analyzeScalars(const float* L, const float* R, int n)`, and `const AnalysisFrame& frame() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/strx_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_dsp.h"
#include <cmath>
#include <vector>

using namespace Seam::strx;

// Drive the analyzer to steady state with a generated stereo signal.
static AnalysisFrame settle(std::function<void(int,float&,float&)> gen) {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(512), R(512);
    for (int blk = 0; blk < 400; ++blk) {              // ~4 s at 48k/512
        for (int i = 0; i < 512; ++i) gen(blk*512+i, L[i], R[i]);
        a.analyzeScalars(L.data(), R.data(), 512);
    }
    return a.frame();
}

TEST_CASE("mono (L=R): correlation ~1, width ~0, side at floor") {
    auto f = settle([](int n, float& l, float& r){
        l = r = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); });
    CHECK(f.correlation == doctest::Approx(1.0).epsilon(0.02));
    CHECK(f.width       == doctest::Approx(0.0).epsilon(0.02));
    CHECK(f.side        <= -55.0f);
    CHECK(f.panorama    == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("anti-phase (L=-R): correlation ~-1, width ~1, mid at floor") {
    auto f = settle([](int n, float& l, float& r){
        l = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); r = -l; });
    CHECK(f.correlation == doctest::Approx(-1.0).epsilon(0.02));
    CHECK(f.width       == doctest::Approx(1.0).epsilon(0.02));
    CHECK(f.mid         <= -55.0f);
}

TEST_CASE("left only: panorama fully left (~-1)") {
    auto f = settle([](int n, float& l, float& r){
        l = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); r = 0.0f; });
    CHECK(f.panorama == doctest::Approx(-1.0).epsilon(0.05));
}
```

- [ ] **Step 2: Register and run to verify it fails**

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(strx_dsp_test
    strx_dsp_test.cpp
)
target_include_directories(strx_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(strx_dsp_test PRIVATE cxx_std_17)
add_test(NAME strx_dsp_test COMMAND strx_dsp_test)
```

Run:
```bash
cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target strx_dsp_test 2>&1 | tail -5
```
Expected: FAIL — `strx_dsp.h` not found.

- [ ] **Step 3: Write minimal implementation**

Create `plugins/strx/source/strx_dsp.h`:

```cpp
// SEAM-LTM · strx_dsp — SDK-free analysis core for the STONE observation analyzer.
//
// FAUST REFERENCE:
//   seam.stereophony.lib : sst.sdmx   — Blumlein M/S sum-and-difference matrix
//   seam.analyzers.lib   : san.correlation, san.width, san.panorama, san.vectorangle
//   seam.analyzers.lib   : an.mth_octave_spectral_level (idiomatic spectrum equiv.)
// The spectral curve here uses a Welch FFT (seam_fft.h) for a finer display,
// citing the filterbank analyzer as the SR-independent Faust equivalent.
#pragma once
#include "seam_meter.h"
#include <cmath>

namespace Seam { namespace strx {

struct AnalysisFrame {
    float inL = -60.f, inR = -60.f, mid = -60.f, side = -60.f;
    float correlation = 0.f;   // [-1,1]
    float width       = 0.f;   // [0,1], 0 = mono
    float panorama    = 0.f;   // [-1,1]
    float angleRad    = 0.f;
};

class Analyzer {
public:
    void prepare(double fs) {
        fs_ = (fs > 0.0) ? fs : 48000.0;
        lvlL_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlR_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlM_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        lvlS_.prepare(fs_, seam::meter::LevelFollower::Mode::Rms, 300.0);
        // one-pole for the covariance means (0.3 s, matching san sfa_tau)
        coef_ = std::exp(-1.0 / (0.3 * fs_));
        reset();
    }
    void reset() {
        lvlL_.reset(); lvlR_.reset(); lvlM_.reset(); lvlS_.reset();
        mLR_ = mLL_ = mRR_ = 0.0;
        frame_ = AnalysisFrame{};
    }
    void analyzeScalars(const float* L, const float* R, int n) {
        double rmsL=0, rmsR=0, rmsM=0, rmsS=0;
        for (int i = 0; i < n; ++i) {
            const double l = L[i], r = R[i];
            const double m = (l + r) * 0.70710678, s = (l - r) * 0.70710678; // sst.sdmx
            rmsL = lvlL_.feed(float(l)); rmsR = lvlR_.feed(float(r));
            rmsM = lvlM_.feed(float(m)); rmsS = lvlS_.feed(float(s));
            mLR_ = l*r + coef_*(mLR_ - l*r);   // moving means (san.correlation)
            mLL_ = l*l + coef_*(mLL_ - l*l);
            mRR_ = r*r + coef_*(mRR_ - r*r);
        }
        const double eps = 1e-12;
        frame_.inL = float(seam::meter::lin2db(rmsL));
        frame_.inR = float(seam::meter::lin2db(rmsR));
        frame_.mid = float(seam::meter::lin2db(rmsM));
        frame_.side = float(seam::meter::lin2db(rmsS));
        frame_.correlation = float(mLR_ / std::sqrt(std::max(eps, mLL_*mRR_)));
        frame_.width = float(rmsS / std::max(eps, rmsM + rmsS));    // san.width
        frame_.panorama = float((mRR_ - mLL_) / std::max(eps, mRR_ + mLL_)); // san.panorama
        frame_.angleRad = float(0.5 * std::atan2(2.0*mLR_, mLL_ - mRR_));     // san.vectorangle
    }
    const AnalysisFrame& frame() const { return frame_; }
protected:
    double fs_ = 48000.0, coef_ = 0.0;
    double mLR_ = 0, mLL_ = 0, mRR_ = 0;
    seam::meter::LevelFollower lvlL_, lvlR_, lvlM_, lvlS_;
    AnalysisFrame frame_;
};

}} // namespace Seam::strx
```

Add `#include <algorithm>` (for `std::max`) to the includes.

- [ ] **Step 4: Run the tests and verify they pass**

```bash
cmake --build build --target strx_dsp_test 2>&1 | tail -3
ctest --test-dir build -R strx_dsp_test --output-on-failure
```
Expected: PASS (3 test cases).

- [ ] **Step 5: Commit**

```bash
git add plugins/strx/source/strx_dsp.h tests/strx_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(strx): analysis core scalars (M/S, levels, correlation/width/panorama)

Hand port of sst.sdmx + san.correlation/width/panorama/vectorangle, verified
against mono/anti-phase/left-only doctests.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 4: `strx_dsp.h` — goniometer points + Welch M&S spectra

Extend `AnalysisFrame` with the goniometer point cloud and the two spectral arrays, and add the pass that fills them.

**Files:**
- Modify: `plugins/strx/source/strx_dsp.h`
- Modify: `tests/strx_dsp_test.cpp`

**Interfaces:**
- Consumes: `seam::fft::Welch`.
- Produces (added to `AnalysisFrame` and `Analyzer`):
  - `AnalysisFrame`: `static constexpr int kMaxPoints = 1024;` `int numPoints;` `float gx[kMaxPoints], gy[kMaxPoints];` (goniometer, normalized ~[-1,1]); `static constexpr int kNumBins = 2049;` `float specM[kNumBins], specS[kNumBins];` (dB); `int numBins;`.
  - `Analyzer`: `void analyze(const float* L, const float* R, int n)` — calls `analyzeScalars` then fills goniometer + spectra; `int fftSize()` const (4096).

- [ ] **Step 1: Write the failing test**

Append to `tests/strx_dsp_test.cpp`:

```cpp
TEST_CASE("goniometer: mono maps to the vertical axis (x~0, y!=0)") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    for (int i = 0; i < 1024; ++i)
        L[i] = R[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    a.analyze(L.data(), R.data(), 1024);
    const auto& f = a.frame();
    REQUIRE(f.numPoints > 0);
    float maxAbsX = 0, maxAbsY = 0;
    for (int i = 0; i < f.numPoints; ++i) {
        maxAbsX = std::max(maxAbsX, std::fabs(f.gx[i]));
        maxAbsY = std::max(maxAbsY, std::fabs(f.gy[i]));
    }
    CHECK(maxAbsX < 1e-3f);      // S = 0 on the horizontal axis
    CHECK(maxAbsY > 0.1f);       // M carries the energy (vertical)
}

TEST_CASE("spectrum: a 1 kHz mid tone peaks near the 1 kHz bin") {
    Analyzer a; a.prepare(48000.0);
    const int N = a.fftSize();
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i)
        L[i] = R[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    for (int b = 0; b < 8; ++b) a.analyze(L.data(), R.data(), N);
    const auto& f = a.frame();
    const int expBin = int(std::lround(1000.0 * N / 48000.0));
    int peak = 1;
    for (int k = 2; k < f.numBins; ++k) if (f.specM[k] > f.specM[peak]) peak = k;
    CHECK(std::abs(peak - expBin) <= 2);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target strx_dsp_test 2>&1 | tail -5
```
Expected: FAIL — `analyze`, `fftSize`, `numPoints`, `specM` undefined.

- [ ] **Step 3: Write minimal implementation**

In `strx_dsp.h`, add `#include "seam_fft.h"`, extend `AnalysisFrame` with the arrays from Interfaces, and add to `Analyzer`:

```cpp
    // in prepare(), after the level followers:
    welchM_.prepare(kFftSize, 2.0, fs_);   // τ = 2 s live EMA
    welchS_.prepare(kFftSize, 2.0, fs_);

    int fftSize() const { return kFftSize; }

    void analyze(const float* L, const float* R, int n) {
        analyzeScalars(L, R, n);
        // Goniometer: (x=S, y=M), decimate by stride to <= kMaxPoints.
        const int stride = (n + AnalysisFrame::kMaxPoints - 1) / AnalysisFrame::kMaxPoints;
        int p = 0;
        for (int i = 0; i < n && p < AnalysisFrame::kMaxPoints; i += (stride > 0 ? stride : 1)) {
            const float s = (L[i] - R[i]) * 0.70710678f;
            const float m = (L[i] + R[i]) * 0.70710678f;
            frame_.gx[p] = s; frame_.gy[p] = m; ++p;
        }
        frame_.numPoints = p;
        // Spectra: feed both Welch analyzers per sample.
        for (int i = 0; i < n; ++i) {
            const float m = (L[i] + R[i]) * 0.70710678f;
            const float s = (L[i] - R[i]) * 0.70710678f;
            welchM_.push(m); welchS_.push(s);
        }
        frame_.numBins = welchM_.numBins();
        const float* mM = welchM_.magnitudeDb();
        const float* mS = welchS_.magnitudeDb();
        for (int k = 0; k < frame_.numBins; ++k) { frame_.specM[k] = mM[k]; frame_.specS[k] = mS[k]; }
    }
```

Add members and constant to `Analyzer`:

```cpp
    static constexpr int kFftSize = 4096;   // kNumBins = 2049 in AnalysisFrame
    seam::fft::Welch welchM_, welchS_;
```

Also call `welchM_.reset(); welchS_.reset();` in `reset()`.

- [ ] **Step 4: Run the tests and verify they pass**

```bash
cmake --build build --target strx_dsp_test 2>&1 | tail -3
ctest --test-dir build -R strx_dsp_test --output-on-failure
```
Expected: PASS (5 test cases).

- [ ] **Step 5: Commit**

```bash
git add plugins/strx/source/strx_dsp.h tests/strx_dsp_test.cpp
git commit -m "feat(strx): goniometer decimation + Welch M/S spectra in AnalysisFrame

analyze() fills (S,M) points and both M/S spectral curves. Verified: mono
maps to the vertical goniometer axis; a 1 kHz mid tone peaks at its bin.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 5: `strx_dsp.h` — lock-free triple-buffer publish/read

Wrap the frame in a triple-buffer so the audio thread publishes and the GUI thread reads without locks or tearing.

**Files:**
- Modify: `plugins/strx/source/strx_dsp.h`
- Modify: `tests/strx_dsp_test.cpp`

**Interfaces:**
- Produces (on `Analyzer`):
  - `void process(const float* L, const float* R, int n)` — audio thread: `analyze` into the write slot, then publish (atomic).
  - `bool tryReadFrame(AnalysisFrame& out)` — GUI thread: copy the latest published slot; returns false if nothing new since the last read.

- [ ] **Step 1: Write the failing test**

Append to `tests/strx_dsp_test.cpp`:

```cpp
TEST_CASE("triple-buffer: read after process returns the latest frame") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    for (int i = 0; i < 1024; ++i) { L[i] = 0.5f; R[i] = 0.0f; }  // left only
    a.process(L.data(), R.data(), 1024);
    AnalysisFrame out;
    REQUIRE(a.tryReadFrame(out));
    CHECK(out.panorama == doctest::Approx(-1.0).epsilon(0.1));
    CHECK_FALSE(a.tryReadFrame(out));    // nothing new since last read
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --target strx_dsp_test 2>&1 | tail -5
```
Expected: FAIL — `process`, `tryReadFrame` undefined.

- [ ] **Step 3: Write minimal implementation**

Refactor `Analyzer` so `analyze` writes into `slots_[write_]` instead of a single `frame_`. Add a triple-buffer:

```cpp
    // members
    AnalysisFrame slots_[3];
    int write_ = 0;
    std::atomic<int> ready_{-1};     // last published slot, -1 = none
    int lastRead_ = -1;

    // change analyze()/analyzeScalars() to fill `slots_[write_]` (via a
    // `AnalysisFrame& fr = slots_[write_];` alias) instead of `frame_`.

    void process(const float* L, const float* R, int n) {
        analyze(L, R, n);                 // fills slots_[write_]
        ready_.store(write_, std::memory_order_release);
        write_ = (write_ + 1) % 3;
        if (write_ == lastRead_) write_ = (write_ + 1) % 3;  // skip the slot GUI holds
    }
    bool tryReadFrame(AnalysisFrame& out) {
        const int r = ready_.load(std::memory_order_acquire);
        if (r < 0 || r == lastRead_) return false;
        out = slots_[r];
        lastRead_ = r;
        return true;
    }
```

Add `#include <atomic>`. Keep `frame()` returning `slots_[write_ == 0 ? 2 : write_-1]` (the last written) for the existing scalar/spectrum tests, or update those tests to read via `tryReadFrame`. Simplest: keep a `const AnalysisFrame& frame() const { return slots_[(write_+2)%3]; }` accessor for tests.

- [ ] **Step 4: Run the full core test suite and verify it passes**

```bash
cmake --build build --target strx_dsp_test 2>&1 | tail -3
ctest --test-dir build -R "strx_dsp_test|seam_fft_test" --output-on-failure
```
Expected: PASS (all cases across both cores).

- [ ] **Step 5: Commit**

```bash
git add plugins/strx/source/strx_dsp.h tests/strx_dsp_test.cpp
git commit -m "feat(strx): lock-free triple-buffer publish/read for AnalysisFrame

Audio thread publishes with a release store; GUI thread reads the latest
complete slot with an acquire load. No locks, no tearing.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 6: strx plugin scaffold — ids, processor, pass-through, build

The loadable plugin with no custom views yet: stereo bus, audio pass-through, the analyzer wired in `process`, and the default editor. Establishes the build and validator baseline.

**Files:**
- Create: `plugins/strx/source/strx_ids.h`, `version.h`, `strx_processor.h`, `strx_processor.cpp`
- Create: `plugins/strx/CMakeLists.txt`
- Create: `plugins/strx/resource/strx.uidesc` (minimal, background only for now)
- Modify: root `CMakeLists.txt` (`add_subdirectory(plugins/strx)`)
- Create: `plugins/strx/doc/README.md`

**Interfaces:**
- Consumes: `Seam::strx::Analyzer`.
- Produces: `Seam::StrxProcessor` (`SingleComponentEffect` + `VST3EditorDelegate`); a fresh UID and class name registered in the module factory.

- [ ] **Step 1: Read the dslar scaffold to mirror**

Read `plugins/dslar/source/dslar_ids.h`, `dslar_processor.h`, `dslar_processor.cpp` (factory, `initialize`, `setBusArrangements`, `process`, `createView`), `plugins/dslar/CMakeLists.txt`, and the `add_subdirectory(plugins/dslar)` line in the root `CMakeLists.txt`. `strx` follows the same shape with: **stereo in / stereo out**, **no parameters**, audio **passed through unchanged**.

- [ ] **Step 2: Write `strx_ids.h` (fresh UID)**

Create `plugins/strx/source/strx_ids.h`:

```cpp
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {
// Generate a unique GUID (macOS: `uuidgen`) and split into 4 hex groups.
static const Steinberg::FUID kStrxProcessorUID(0x00000000, 0x00000000, 0x00000000, 0x00000000);
// Custom-view name tags (match resource/strx.uidesc custom-view "name" attrs).
static const char* kViewGoniometer = "StrxGoniometer";
static const char* kViewSpectrum    = "StrxSpectrum";
static const char* kViewMeters      = "StrxMeters";
} // namespace Seam
```

Replace the zeroed UID with a real one from `uuidgen`.

- [ ] **Step 3: Write the processor**

Create `plugins/strx/source/strx_processor.h` mirroring `dslar_processor.h` but: no parameter atomics; hold a `Seam::strx::Analyzer analyzer_;`. Create `strx_processor.cpp` with:
- factory registration (`BEGIN_FACTORY_DEF` / module `GetPluginFactory` per the suite pattern in `dslar_processor.cpp`),
- `initialize`: add one stereo audio input and one stereo audio output bus,
- `setBusArrangements`: accept only `2 in / 2 out` (stereo), else `kResultFalse`,
- `setupProcessing`: `analyzer_.prepare(setup.sampleRate)`,
- `process`: copy input to output **unchanged** (pass-through), then `analyzer_.process(inL, inR, numSamples)`,
- `createView`: return a `VST3Editor(this, "editor", "strx.uidesc")` (as `dslar` does),
- `setState`/`getState`: no-ops returning `kResultOk`.

The DSP FAUST REFERENCE block lives in `strx_dsp.h`; the processor header opens with a one-line pointer comment to it.

- [ ] **Step 4: Write `version.h`, minimal `strx.uidesc`, CMakeLists, register**

- `version.h`: copy `plugins/dslar/source/version.h`, change name strings to `strx`.
- `resource/strx.uidesc`: copy the `<vstgui-ui-description>` skeleton from `plugins/dslar/resource/dslar.uidesc`, keep just a sized background `view` (three-zone frame comes in Tasks 7–9). Set the editor size wide enough for three zones (e.g. 900×360).
- `CMakeLists.txt`: copy `plugins/dslar/CMakeLists.txt`, replace `dslar`→`strx`, description, and `BUNDLE_IDENTIFIER "io.github.s-e-a-m.strx"`; keep the `../_common` include and the logo/font resources.
- Root `CMakeLists.txt`: add `add_subdirectory(plugins/strx)` next to the other plugins.

- [ ] **Step 5: Build, validate, verify pass-through**

```bash
cmake -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk
cmake --build build --target strx 2>&1 | tail -15
# validator (path per the suite; validator target builds with the SDK)
./build/bin/Debug/validator "$(find build -name 'strx.vst3' | head -1)" 2>&1 | tail -20
```
Expected: build succeeds; validator reports all tests passed (parity with `dslar`). Confirm in a host that audio passes through unchanged (null test: signal in == signal out).

- [ ] **Step 6: Commit**

```bash
git add plugins/strx CMakeLists.txt
git commit -m "feat(strx): loadable stereo pass-through analyzer scaffold

SingleComponentEffect, stereo in/out, no parameters, audio passed through
unchanged; Analyzer wired in process(). Validator clean.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 7: `StrxMeters` view + meters zone

First custom view: the meter column (In L/R · M · S bars + Width bar), fed by the triple-buffer on a GUI timer.

**Files:**
- Create: `plugins/strx/source/strx_meters.h`
- Modify: `plugins/strx/source/strx_processor.{h,cpp}` (`createCustomView`, hand the view a way to pull frames)
- Modify: `plugins/strx/resource/strx.uidesc` (meters zone + custom-view tag)

**Interfaces:**
- Consumes: `Analyzer::tryReadFrame`, `seam::meter::db2norm`.
- Produces: `class Seam::StrxMeters : public VSTGUI::CView` with a `CVSTGUITimer` polling `tryReadFrame` and drawing bars; constructed in `createCustomView` for name `kViewMeters`.

- [ ] **Step 1: Read the custom-view pattern**

Read `plugins/dslar/source/dslar_reset_button.h` (a `CView` subclass with a `CVSTGUITimer`, `draw()`, and `createCustomView` wiring in `dslar_processor.cpp`). `StrxMeters` follows the same structure but reads `AnalysisFrame` and draws bars.

- [ ] **Step 2: Implement `StrxMeters`**

Create `plugins/strx/source/strx_meters.h`: a `CView` holding a pointer/getter to the processor's latest frame (add `bool StrxProcessor::latestFrame(AnalysisFrame&)` forwarding to `analyzer_.tryReadFrame`, cached so multiple views can read). In `draw(CDrawContext*)`:
- map each level (`inL,inR,mid,side`, dBFS) with `seam::meter::db2norm(db, -60)` to a bar height,
- draw four labelled vertical bars (In L, In R, M, S) with a dB scale,
- draw the Width bar: fill height from `frame.width` (0=mono at bottom → 1 at top), and when `frame.correlation < 0` tint the top ("inv") region.
Drive repaint from a `CVSTGUITimer` at ~30 Hz calling `invalid()`.

- [ ] **Step 3: Wire `createCustomView` and the uidesc**

In `strx_processor.cpp::createCustomView`, when `name == kViewMeters` return `new StrxMeters(size, this)`. In `strx.uidesc` add a `<view>` with `class="CView"`/custom-view `name="StrxMeters"` positioned in the right third of the window.

- [ ] **Step 4: Build, validate, verify in host**

```bash
cmake --build build --target strx 2>&1 | tail -8
./build/bin/Debug/validator "$(find build -name 'strx.vst3' | head -1)" 2>&1 | tail -5
```
Expected: builds, validator clean. In Reaper: feed pink → M and S bars move; a mono signal → S bar drops to floor, Width shows "mono"; anti-phase → Width shows "inv".

- [ ] **Step 5: Commit**

```bash
git add plugins/strx/source/strx_meters.h plugins/strx/source/strx_processor.h plugins/strx/source/strx_processor.cpp plugins/strx/resource/strx.uidesc
git commit -m "feat(strx): StrxMeters view — In L/R, M, S bars + Width bar

Custom CView polling the triple-buffer; reuses seam_meter db2norm. Width bar
shows mono/100%/inv from width + correlation sign.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 8: `StrxGoniometer` view + goniometer zone

The decaying L/R scatter with Angle/Panorama readout.

**Files:**
- Create: `plugins/strx/source/strx_goniometer.h`
- Modify: `plugins/strx/source/strx_processor.cpp` (`createCustomView` for `kViewGoniometer`)
- Modify: `plugins/strx/resource/strx.uidesc` (goniometer zone, left third)

**Interfaces:**
- Consumes: `StrxProcessor::latestFrame`, `CGraphicsPath`.
- Produces: `class Seam::StrxGoniometer : public VSTGUI::CView` with a decaying point history.

- [ ] **Step 1: Implement `StrxGoniometer`**

Create `plugins/strx/source/strx_goniometer.h`: a `CView` keeping a ring of recent frames' points (e.g. last 8 frames) each tagged with an age; on `latestFrame`, push the new points and age older ones. In `draw()`:
- draw the bounding circle and L/R diagonal axes + a light grid,
- for each stored point map `(gx=S, gy=M)` to view coordinates (center origin, y up), and stroke via `CGraphicsPath` with alpha decreasing by age (the Melda-style trail),
- overlay `ANGLE {deg}°   PANORAMA {pct}%` from `frame.angleRad` (→ degrees) and `frame.panorama` (→ percent) at the bottom.
Repaint on a `CVSTGUITimer` ~30 Hz.

Decay depth and per-frame alpha are `static constexpr` constants at the top (tunable, like `kRampMs` in dslar).

- [ ] **Step 2: Wire and lay out**

`createCustomView`: `name == kViewGoniometer` → `new StrxGoniometer(size, this)`. In `strx.uidesc` add a square custom-view `name="StrxGoniometer"` in the left third.

- [ ] **Step 3: Build, validate, verify in host**

```bash
cmake --build build --target strx 2>&1 | tail -8
./build/bin/Debug/validator "$(find build -name 'strx.vst3' | head -1)" 2>&1 | tail -5
```
Expected: builds, validator clean. In Reaper: decorrelated pink → wide cloud; mono → vertical line; anti-phase → horizontal line; the trail decays smoothly.

- [ ] **Step 4: Commit**

```bash
git add plugins/strx/source/strx_goniometer.h plugins/strx/source/strx_processor.cpp plugins/strx/resource/strx.uidesc
git commit -m "feat(strx): StrxGoniometer view — decaying L/R scatter + Angle/Panorama

CGraphicsPath scatter with per-frame alpha trail; (S,M) rotation so mono is
vertical and anti-phase horizontal.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 9: `StrxSpectrum` view + spectrum zone

The overlaid M and S spectral curves.

**Files:**
- Create: `plugins/strx/source/strx_spectrum.h`
- Modify: `plugins/strx/source/strx_processor.cpp` (`createCustomView` for `kViewSpectrum`)
- Modify: `plugins/strx/resource/strx.uidesc` (spectrum zone, center third)

**Interfaces:**
- Consumes: `StrxProcessor::latestFrame`, `CGraphicsPath`.
- Produces: `class Seam::StrxSpectrum : public VSTGUI::CView`.

- [ ] **Step 1: Implement `StrxSpectrum`**

Create `plugins/strx/source/strx_spectrum.h`: a `CView` that on each timer tick pulls the latest frame and draws two `CGraphicsPath` curves from `specM`/`specS` (`numBins`). X-axis: log-frequency 20 Hz–20 kHz, mapping bin `k` → freq `k*fs/fftSize` → log position (store `fs` and `fftSize` via the processor or a setter). Y-axis: dB from −120 to +6 mapped to height. Draw a light freq/dB grid and an M/S colour legend. Repaint ~30 Hz.

- [ ] **Step 2: Wire and lay out**

`createCustomView`: `name == kViewSpectrum` → `new StrxSpectrum(size, this)`. In `strx.uidesc` add a custom-view `name="StrxSpectrum"` in the center third.

- [ ] **Step 3: Build, validate, verify in host**

```bash
cmake --build build --target strx 2>&1 | tail -8
./build/bin/Debug/validator "$(find build -name 'strx.vst3' | head -1)" 2>&1 | tail -5
```
Expected: builds, validator clean. In Reaper: a 1 kHz tone → both curves peak at 1 kHz; decorrelated pink → broadband M and S curves, Side visibly present across the band.

- [ ] **Step 4: Commit**

```bash
git add plugins/strx/source/strx_spectrum.h plugins/strx/source/strx_processor.cpp plugins/strx/resource/strx.uidesc
git commit -m "feat(strx): StrxSpectrum view — overlaid M/S Welch curves

Two CGraphicsPath curves on a log-frequency axis, 20 Hz–20 kHz, dB scale.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

### Task 10: Final integration — three-zone layout, resources, full verification

Polish the layout, wire the SEAM logo/font, and run the whole verification matrix.

**Files:**
- Modify: `plugins/strx/resource/strx.uidesc` (final three-zone geometry, background, logo)
- Modify: `plugins/strx/doc/README.md` (link the spec, list the `san` citations)

**Interfaces:** none new.

- [ ] **Step 1: Finalize the three-zone layout**

In `strx.uidesc`: goniometer (left) │ spectrum (center) │ meters (right), sized so each plot is large and legible (spec Section 4). Add the SEAM logo and the Source Code Pro font already referenced in the CMake resources (mirror `dslar.uidesc`). Confirm the editor size matches the three-zone frame.

- [ ] **Step 2: Full verification matrix**

```bash
cmake --build build --target strx 2>&1 | tail -8
ctest --test-dir build -R "strx_dsp_test|seam_fft_test" --output-on-failure
./build/bin/Debug/validator "$(find build -name 'strx.vst3' | head -1)" 2>&1 | tail -20
```
Expected: cores PASS; validator all-pass (parity with dslar). Then in Reaper, walk the functional matrix and confirm each:
- decorrelated pink → wide goniometer cloud, high Side energy across the spectrum, Width ~100%;
- mono → vertical goniometer line, Side bar/curve at floor, Width "mono";
- anti-phase (L=−R) → horizontal goniometer line, Width "inv".

- [ ] **Step 3: Lock-free review**

Read `strx_dsp.h::process` and confirm: no locks, no heap allocation, no I/O on the audio path; the triple-buffer uses only atomic index load/store; the GUI views only ever `tryReadFrame`/copy. Note the confirmation in `doc/README.md`.

- [ ] **Step 4: Commit**

```bash
git add plugins/strx/resource/strx.uidesc plugins/strx/doc/README.md
git commit -m "feat(strx): final three-zone layout + verification pass

Goniometer | spectrum | meters, logo/font wired. Cores green, validator
all-pass, functional matrix (pink/mono/anti-phase) confirmed, lock-free path
reviewed. Spec 1 of the STONE auto-calibration system complete.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_017xT12fYtfXPz7ht3X8h61Y"
```

---

## Self-Review

**Spec coverage:**
- Stereo bus, pass-through, always-M/S → Task 6. ✓
- Triple-buffer audio→GUI, no IMessage → Task 5. ✓
- Goniometer decaying scatter + Angle/Panorama → Tasks 4, 8. ✓
- M+S Welch spectrum, live EMA → Tasks 2b, 4, 9. ✓
- Meters In L/R · M · S · Width, sample-peak via seam_meter → Tasks 3, 7. ✓
- Zero VST3 parameters → Task 6. ✓
- Three-zone layout → Tasks 7–9, finalized Task 10. ✓
- Faust anchoring: cite `sst.sdmx`/`an.*`, add `san.correlation/width/panorama/vectorangle` → Task 1, cited in `strx_dsp.h` (Task 3). ✓
- New `_common/seam_fft.h` (reused by Spec 3) → Tasks 2, 2b. ✓
- Verification: validator, Reaper matrix, lock-free review → Task 10. ✓
- Deferred (metering refinement, Spec 2–4) → out of scope, unlisted. ✓

**Placeholder scan:** the only intentional placeholder is the zeroed `FUID` in `strx_ids.h` (Task 6, Step 2), with the explicit instruction to replace it via `uuidgen` — this is a required per-plugin unique value, not a plan gap.

**Type consistency:** `AnalysisFrame` field names (`inL,inR,mid,side,correlation,width,panorama,angleRad,numPoints,gx,gy,specM,specS,numBins`) are used consistently across Tasks 3–9. `Analyzer` methods (`prepare/reset/analyzeScalars/analyze/process/tryReadFrame/frame/fftSize`) are consistent. `seam::fft::transform` and `seam::fft::Welch` (`prepare/reset/push/hasNewFrame/magnitudeDb/numBins`) match between Tasks 2/2b and Task 4. View class names match the `kView*` tags in `strx_ids.h`.

---

## Notes for the implementer

- The `validator` binary path may differ by generator; locate it with `find build -name validator -type f`. The suite target parity is `dslar`'s 47/47.
- If `faust` is not installed, Task 1's Step 3 verification is skipped; the C++ port's doctests (Tasks 3–5) are the binding correctness check.
- Keep tunable constants (goniometer decay depth/alpha, Welch τ, FFT size) as named `static constexpr` at the top of their headers, following `dslar`'s `kRampMs`/`kTickMs` convention.
