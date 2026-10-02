# delRM Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A hand-written C++ VST3 of SSCDO#2's delRM (`sdt.delrmcomb` on channels 1 and 3, `sdt.delrmrm : sdt.delrmdyn` on channels 2 and 4, one prime delay in metres), equal to the Faust spec to numerical precision, with input and right-to-left gain-reduction meters.

**Architecture:** Three new reusable `_common/` headers — `seam_delays.h` (`de.delay`), `seam_filters.h` (`sfi.leakyint`), `seam_compressors.h` (`co.compressor_mono`) — plus `metresToPrimeSamples` added to `seam_primes.h` (`sma.imt2npsamp`), wired by an SDK-free engine `plugins/delrm/source/delrm_dsp.h`, driven by a thin `SingleComponentEffect` processor with a ParamBox of atomics, read-only meter parameters and one footer view. Faust references are rendered once by a committed script into committed headers, so the doctest suite proves C++ == spec without `faust`.

**Tech Stack:** C++17, VST3 SDK + VSTGUI, CMake (Xcode generator), doctest, Faust 2.88 + faustlibraries clone (reference generation only), Python 3 (lint, docs), LuaLaTeX (report).

**Spec:** `docs/superpowers/specs/2026-10-02-delrm-plugin-design.md` (approved 2026-10-02). Read it before any task.

## Global Constraints

- State and arithmetic in `double` throughout; `float`/`double` conversion only at the bus.
- The spec is `sdt.delrmcomb`, `sdt.delrmint`, `sdt.delrmrm`, `sdt.delrmdyn` (seam.tedesco.lib), `sfi.leakyint` (seam.filters.lib), `sma.imt2npsamp` (seam.math.lib), `co.compressor_mono` (faustlibraries 0965ea2 or later). Do not change their DSP.
- D: `mm = floor(mt·1000 + 0.5)/1000`, `n = floor(mm·fs/331.4 + 0.5)`, kept when `n < 2`, else the smallest prime **strictly greater** than n, computed at the session's rate. One D for the four channels.
- Distance 0–30 m, default 7.291 m; the delay jumps on a change (no crossfade).
- Integrator: `y = x/fs + exp(−2π·1/fs)·y[n−1]`, times 96000. Triple product `(x[n−D]·x)·I`, then `·10`, then `co.compressor_mono(11, −24, 0.03, 0.04)`.
- No DC blocker, no high-pass, no internal 0.9 volume: one `output` 0–1 linear (CC82) and POWER, 25 ms linear ramps (`Seam::LinearRamp`).
- Delay lines: `Dmax + 1` doubles each, `Dmax = metresToPrimeSamples(30, fs)`, allocated and zeroed in `setActive(true)`, never in `process()`.
- Filters and DSP blocks are reusable C++ libraries in `plugins/_common/` (Giuseppe): each header cites its Faust, takes generic parameters, knows nothing of delRM, and has its own `tests/seam_<lib>_test.cpp`. `delrm_dsp.h` only wires them.
- Meters: input peak per channel over −70…+5 dB; GR of channels 2 and 4 over 0…48 dB of reduction, drawn right to left (`reverse-orientation="true"`); instant attack per block, 300 ms one-pole release across blocks.
- UI: `doc/style/ui-style.md`, format S (300 px), title `SEAM DELRM`, factory name `SEAM DELRM`, `tools/check-uidesc.py` clean, palette names only (never `TextDim`, in the uidesc or in C++).
- FUID `0x5E4D0012, 0xA1B2C3D4, 0x44524D00, 0x00000012` (word3 = ASCII "DRM\0"); subcategory `Fx|Delay`; bundle id `io.github.s-e-a-m.delrm`.
- Build: `cmake --build build --config Release --target delrm` (Xcode generator, SDK at `/Users/giuseppe/Documents/github/seam/sdk/vst3sdk`). Tests: `cmake --build build-test --config Release && ctest --test-dir build-test -C Release` (`build-test` has `SEAM_BUILD_PLUGINS=OFF`; never build plugins there, it would steal the VST3 symlinks).
- doctest `Approx` is banned for small values (suite trap): use explicit absolute or relative tolerances.
- Code, comments, commits and docs in English (the report stays Italian); one sentence per line in prose docs; affirmative voice.
- Rehearsal decisions go into the report first, then the log, then `seam.tedesco.lib` (Task 1 does this before any code).
- Commit as you go (seam-ltm, faust-libraries); never push and never run `make -C doc publish` without asking Giuseppe.
- Commit trailer on every commit:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01BJeSQ62cDJHEYkrsCjV6dM
  ```

## Review Focus

1. **In-place buffers.** A host may pass the same pointers for input and output; the output must equal the out-of-place output exactly (test in Task 6).
2. **Block sizes the suite never sees.** 1-sample, 7-sample and 4093-sample blocks must give exactly the output of 256-sample blocks, the meters' block values aside (test in Task 6).
3. **A rate change in the session.** `prepare(48000)` after a run at 96 kHz must give D, line length and output of a fresh 48 kHz engine (test in Task 6).
4. **The distance at its ends and off the range.** 0 m gives D = 0 (the comb doubles, the triple product is x²·I); 30 m at 384 kHz finds its prime; a host value of −0.2 or 1.3 normalized, or a distance of −1 or 31 m, clamps to 0 or 30 m (tests in Tasks 2, 6, 7).
5. **Silence after sound.** After the loud part of the signal, digital silence must bring every output to exactly 0 within a few seconds and never produce NaN or Inf (the compressor's `log10` of 0), and the GR meter must return toward 0 (test in Task 6).
6. **process() without memory.** Before `prepare`, after `release`, `process()` writes zeros and never touches the lines (test in Task 6).

---

### Task 1: The decisions, in the report first, then the log, then the library

**Files:**
- Modify: `doc/study/sscdo2/report/preamble.tex` (a `\questionechiusa` macro after `\questione`)
- Modify: `doc/study/sscdo2/report/check.py` (a closed question satisfies a required id)
- Modify: `doc/study/sscdo2/report/parte2-delrm.tex` (cards `delrm-volume`, `delrm-dcblocker`)
- Modify: `doc/study/sscdo2/report/parte3-prove.tex` (questions `dcblocker`, `volume-delrm`)
- Modify: `doc/study/sscdo2/report/parte1-principi.tex:88`
- Create: `logs/2026-10-02-sscdo2-delrm.md`
- Modify (faust-libraries): `src/seam.tedesco.lib` (the delRM header comment)

- [ ] **Step 1: Add the closed question to `preamble.tex`**, right after the `\questione` definition:

```latex
% ---- a question answered: the question, then the answer and its date
\newcommand{\questionechiusa}[3]{%
  \refstepcounter{questione}\label{q:#1}%
  \par\noindent\deciso\ \textbf{Q\thequestione.}\ #2\par
  \noindent\textit{Risposta:} #3\par\medskip}
```

- [ ] **Step 2: Teach `check.py` that a closed question is present.** In `check()`, where `have` is built (line 126), add the closed form to the question ids:

```python
        have = {m: {a[0] for a in args(alltex, m, n)} for m, n in (("scheda", 8), ("prova", 4), ("questione", 2))}
        have["questione"] |= {a[0] for a in args(alltex, "questionechiusa", 3)}
```

`args` matches `\questione(?![A-Za-z])`, so `\questionechiusa` is never read as an open question.
Update the docstring's point 4: "every required \\scheda, \\prova and \\questione id is present (a \\questionechiusa counts)".

- [ ] **Step 3: Close the two questions** in `parte3-prove.tex`. Replace the `\questione{dcblocker}{...}` line with:

```latex
\questionechiusa{dcblocker}{Il basso più sottile che davano i due DC blocker di delRM va tenuto, come due passa-alto dichiarati a \qty{76.59}{\hertz} in serie? (scheda~\ref{scheda:delrm-dcblocker})}{il porting procede senza; si ascolta in prova e si valuta (prova~\ref{prova:delrm-dcblocker}). Giuseppe e Davide, 2 ottobre 2026, dopo la lettura di questo documento.}
```

and the `\questione{volume-delrm}{...}` line with:

```latex
\questionechiusa{volume-delrm}{Secondo la lettura del patch il fader CC82 muove uno stadio di guadagno di Pd; il volume interno di delRM parte da 0 nel wrapper e da 0,9 nel file \texttt{.dsp}: con quale valore interno suonava delRM? (scheda~\ref{scheda:delrm-volume})}{nel porting c'è un solo volume, il fader \texttt{output} del plugin da 0 a 1, mosso dal CC82; lo 0,9 interno non esiste più. Giuseppe e Davide, 2 ottobre 2026.}
```

- [ ] **Step 4: Update the two cards** in `parte2-delrm.tex`.
`delrm-volume`: fields 3–8 become

```latex
{fader lineare da 0 a 1 nel plugin, mosso dal CC82, con una rampa di 25 ms}
{0}
{nessuna oltre al livello}
{l'originale aveva due volumi in serie, lo stadio di guadagno di Pd e il volume interno del \texttt{.dsp} (0 nel wrapper, 0,9 nel file); il porting ne tiene uno solo, e lo 0,9 interno non esiste più (domanda~\ref{q:volume-delrm})}
{\deciso}
{log di sessione, ``Build flags''; \file{logs/2026-10-02-sscdo2-delrm.md}}
```

`delrm-dcblocker`: field 6 becomes `{tolti perché il loro problema non esiste più (\S\ref{sec:principio-processo}); il porting procede senza, e l'ascolto in prova decide se quel basso più sottile va ripreso, come due \texttt{fi.dcblockerat(76.59)} dichiarati (domanda~\ref{q:dcblocker})}`, field 7 becomes `{\daprovare}`.
In `parte1-principi.tex:88` replace the sentence with: `Il porting procede senza; tenere quel basso più sottile si decide all'ascolto in prova (scheda~\ref{scheda:delrm-dcblocker}).`

- [ ] **Step 5: Build and check.** Run: `make -C doc/study/sscdo2/report check`
Expected: the build passes, `check.py --selftest` all ok, `CHECK OK`. The list of states now shows `delrm-volume` DECISO and `delrm-dcblocker` DA PROVARE; Q4 and Q5 carry DECISO and their answers (open the PDF on those pages).

- [ ] **Step 6: Start the log** `logs/2026-10-02-sscdo2-delrm.md`, one sentence per line:

```markdown
# 2026-10-02 — SSCDO#2: the delRM plugin

Third C++ port of SSCDO#2, after LMO and stunedrev (`logs/2026-10-01-sscdo2-plugins.md`).
Spec: `docs/superpowers/specs/2026-10-02-delrm-plugin-design.md`; plan: `docs/superpowers/plans/2026-10-02-delrm-plugin.md`.

## Decisions (Giuseppe and Davide, after reading the report together)

- DC blockers: none; the port proceeds without and the rehearsal's listening judges the thinner bass (card `delrm-dcblocker`, DA PROVARE; question closed).
- Volume: one `output` fader 0–1 in the plugin, moved by CC82; the `.dsp`'s internal 0.9 no longer exists (card `delrm-volume`, DECISO).
- Meters: input peak on the four channels, as the original; the gain reduction of channels 2 and 4, drawn right to left so that the input rising and the compressor descending read as opposite movements.
- One four-channel plugin; new `_common/` blocks `seam_delays.h` (`de.delay`), `seam_filters.h` (`sfi.leakyint`), `seam_compressors.h` (`co.compressor_mono`); `metresToPrimeSamples` joins `seam_primes.h`.
- The delay lines are sized exactly for 30 m at the session's rate: the spec's `1 << 15` holds 30 m up to 192 kHz (17 383 samples), not at 384 kHz (34 763).

## Work
```

- [ ] **Step 7: The library comment** (faust-libraries, `src/seam.tedesco.lib`, the `SSCDO#2 — delRM` header). Replace the last sentence block from "That thinner low end is what the performance sounded like; ..." to "(seam-ltm `doc/study/sscdo2/delrm-dcblock/`)." with:

```
// clarinet's 29.7 Hz fundamental. That thinner low end is what the
// performance sounded like. Decided with Davide (2026-10-02): the port goes
// without, and the rehearsal's listening judges it; if it is wanted, it
// enters as a declared `fi.dcblockerat(76.59)`, not as a DC blocker
// (seam-ltm `doc/study/sscdo2/delrm-dcblock/`).
```

Keep the line before (`... and the two took 17.6 dB from the`) intact so the sentence joins. Check: `faust -I <faustlibraries> -I src -e ...` is not needed for a comment; run `grep -n "Decided with Davide" src/seam.tedesco.lib`.

- [ ] **Step 8: Commit** (two repositories).

```bash
git add doc/study/sscdo2/report logs/2026-10-02-sscdo2-delrm.md
git commit -m "docs(sscdo2/report): delRM volume decided, DC blockers to the listening; two questions closed"
git -C ../faust-libraries add src/seam.tedesco.lib
git -C ../faust-libraries commit -m "seam.tedesco.lib: delRM goes without DC blockers, judged by listening (with Davide)"
```

Commit the rebuilt PDF with the sources (the sources changed, so the rebuild is meaningful).

---

### Task 2: `seam_primes.h` gains metres; `seam_delays.h`

**Files:**
- Modify: `plugins/_common/seam_primes.h` (append `metresToPrimeSamples`)
- Modify: `tests/seam_primes_test.cpp` (append cases)
- Create: `plugins/_common/seam_delays.h`
- Create: `tests/seam_delays_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a block)

**Interfaces:**
- Consumes: `Seam::PrimeSieve` (`isPrime`, `nextPrimeAbove`, `bound`).
- Produces: `constexpr double Seam::kSpeedOfSoundInterior = 331.4;` `uint32_t Seam::metresToPrimeSamples(double mt, double fs, const PrimeSieve&)`.
- Produces: `class Seam::IntegerDelay { void attach(double* buf, std::size_t len); void clear(); void setDelay(uint32_t d); uint32_t delay() const; std::size_t length() const; double tick(double x); }`.

- [ ] **Step 0: Reconfigure the test tree at the 11.0 floor** (the `build-test` cache may still hold 15.7).

Run: `cmake -S . -B build-test -DSEAM_BUILD_PLUGINS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk && cmake --build build-test --config Release && ctest --test-dir build-test -C Release 2>&1 | tail -3`
Expected: every test passes. If `minos_lint` fails, record the output in the log and go on (it predates this branch).

- [ ] **Step 1: Write the failing primes cases**, appended to `tests/seam_primes_test.cpp` (it already defines `trialPrime`):

```cpp
// ── sma.imt2npsamp: metres to a prime number of samples (delRM, DDELAY) ──
// The Faust, transcribed: mm rounded to the millimetre, 331.4 m/s, rounded
// to the sample, the prime strictly above (nextprime.h by trial division).
static uint32_t imt2npsampRef(double mt, double fs) {
    const double mm = std::floor(mt * 1000.0 + 0.5) / 1000.0;
    const long n = (long)std::floor(mm * fs / 331.4 + 0.5);
    if (n < 2) return n < 0 ? 0u : (uint32_t)n;
    uint32_t c = (uint32_t)n + 1;
    while (!trialPrime(c)) ++c;
    return c;
}

TEST_CASE("metresToPrimeSamples equals sma.imt2npsamp over 0-30 m in 1 mm steps") {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        const Seam::PrimeSieve s((uint32_t)std::floor(30.0 * fs / 331.4 + 0.5) + 1024u);
        long bad = 0;
        for (int mm = 0; mm <= 30000; ++mm) {
            const double mt = mm / 1000.0;
            if (Seam::metresToPrimeSamples(mt, fs, s) != imt2npsampRef(mt, fs)) ++bad;
        }
        CAPTURE(fs);
        CHECK(bad == 0);
    }
}

TEST_CASE("metresToPrimeSamples: the values the library comments quote") {
    const Seam::PrimeSieve s(40000);
    CHECK(Seam::metresToPrimeSamples(7.291, 96000.0, s) == 2113);   // Davide's 22 ms at 96 kHz
    CHECK(Seam::metresToPrimeSamples(7.291, 48000.0, s) == 1061);
    CHECK(Seam::metresToPrimeSamples(30.0, 192000.0, s) == 17383);
    CHECK(Seam::metresToPrimeSamples(30.0, 384000.0, s) == 34763);  // past the spec's 1 << 15
    CHECK(Seam::metresToPrimeSamples(0.0, 96000.0, s) == 0);         // below 2: kept as it is
    CHECK(Seam::metresToPrimeSamples(0.003, 96000.0, s) == 1);       // 0.869 -> 1, kept
}

TEST_CASE("metresToPrimeSamples rounds to the millimetre before converting") {
    const Seam::PrimeSieve s(40000);
    // 7.2914 m and 7.2906 m are both 7.291 m to the millimetre.
    CHECK(Seam::metresToPrimeSamples(7.2914, 96000.0, s) == 2113);
    CHECK(Seam::metresToPrimeSamples(7.2906, 96000.0, s) == 2113);
}
```

Add `#include <cmath>` at the top if it is missing.

- [ ] **Step 2: Run, see RED.** Run: `cmake --build build-test --config Release --target seam_primes_test 2>&1 | tail -5`
Expected: compile error, `no member named 'metresToPrimeSamples' in namespace 'Seam'`.

- [ ] **Step 3: Implement**, appended to `plugins/_common/seam_primes.h` before the closing namespace; also add to its header block, under `sma.ms2npsamp`:
```
//   sma.imt2npsamp = select2(n < 2, n : sff.np, n)
//                    with { mm = floor(mt*1000 + 0.5)/1000;
//                           n  = int(floor(mm*ma.SR/isos + 0.5)); };   isos = 331.4
```

```cpp
// sma.isos: the interior speed of sound of seam.math.lib, in m/s.
constexpr double kSpeedOfSoundInterior = 331.4;

// sma.imt2npsamp: a distance in metres to a prime number of samples at fs,
// as DDELAY computes it: the millimetre of a laser distance meter, the
// interior speed of sound, the sample, the prime strictly above.
inline uint32_t metresToPrimeSamples(double mt, double fs, const PrimeSieve& s) {
    const double mm = std::floor(mt * 1000.0 + 0.5) / 1000.0;
    const long n = (long)std::floor(mm * fs / kSpeedOfSoundInterior + 0.5);
    if (n < 2) return n < 0 ? 0u : (uint32_t)n;
    return s.nextPrimeAbove((uint32_t)n);
}
```

- [ ] **Step 4: Run, see GREEN.** Run: `cmake --build build-test --config Release --target seam_primes_test && ctest --test-dir build-test -C Release -R seam_primes_test`
Expected: PASS.

- [ ] **Step 5: Write the failing delay test** `tests/seam_delays_test.cpp`:

```cpp
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
```

Append to `tests/CMakeLists.txt`:

```cmake
# seam_delays.h: de.delay for an integer delay (delRM, and any plugin with a
# plain delay line).
add_executable(seam_delays_test seam_delays_test.cpp)
target_include_directories(seam_delays_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_delays_test PRIVATE cxx_std_17)
add_test(NAME seam_delays_test COMMAND seam_delays_test)
```

- [ ] **Step 6: Run, see RED.** Run: `cmake -S . -B build-test >/dev/null && cmake --build build-test --config Release --target seam_delays_test 2>&1 | tail -3`
Expected: `fatal error: 'seam_delays.h' file not found`.

- [ ] **Step 7: Implement** `plugins/_common/seam_delays.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_delays.h — an integer delay line, the C++ side of de.delay
//
// FAUST REFERENCE (delays.lib, de, standard):
//   de.delay(maxdel, d) : y[n] = x[n-d], d an integer, 0 <= d <= maxdel
//
// The standard library has the delay, so SEAM's Faust has no delays.lib of
// its own (2026-09-29); C++ has none, and a plugin must not define one
// (filters and blocks live in _common/). The buffer belongs to the caller,
// sized exactly for the longest d it will ask (len >= dmax + 1), as
// seam_moorer.h takes it. Written 2026-10-02 for SSCDO#2's delRM; DDELAY and
// ADDELAY still carry their own ring buffers.
//
// Per sample: write x, then read d samples back, so d = 0 returns x itself
// as de.delay does, and a new d is heard on the next tick.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace Seam {

class IntegerDelay {
public:
    // buf holds len doubles, zeroed by the caller; len >= the longest d + 1.
    void attach(double* buf, std::size_t len) { buf_ = buf; len_ = len; clear(); setDelay(d_); }

    // The write index back to the start, the history to zero.
    void clear() {
        for (std::size_t i = 0; i < len_; ++i) buf_[i] = 0.0;
        pos_ = 0;
    }

    // 0 <= d <= len - 1; a longer d is clamped (asserted in debug).
    void setDelay(uint32_t d) {
        assert(len_ == 0 || d < len_);
        d_ = (len_ > 0 && d >= len_) ? (uint32_t)(len_ - 1) : d;
    }
    uint32_t    delay() const  { return d_; }
    std::size_t length() const { return len_; }

    double tick(double x) {
        buf_[pos_] = x;
        std::size_t r = pos_ + len_ - d_;
        if (r >= len_) r -= len_;
        const double y = buf_[r];
        if (++pos_ == len_) pos_ = 0;
        return y;
    }

private:
    double*     buf_ = nullptr;
    std::size_t len_ = 0, pos_ = 0;
    uint32_t    d_ = 0;
};

} // namespace Seam
```

Note: the clamp test sets 100 on a length-8 line; the `assert` would abort a Debug build, and the suite tests in Release (`-C Release`, `NDEBUG`), which is the configuration this test pins. Keep the assert: it catches a sizing bug while developing.

- [ ] **Step 8: Run, see GREEN.** Run: `cmake --build build-test --config Release --target seam_delays_test && ctest --test-dir build-test -C Release -R "seam_delays_test|seam_primes_test"`
Expected: both PASS.

- [ ] **Step 9: Commit.**

```bash
git add plugins/_common/seam_primes.h plugins/_common/seam_delays.h tests/seam_primes_test.cpp tests/seam_delays_test.cpp tests/CMakeLists.txt
git commit -m "feat(_common): metresToPrimeSamples (sma.imt2npsamp) and seam_delays.h (de.delay)"
```

---

### Task 3: The Faust references

**Files:**
- Create: `tests/delrm_signal.h` (the test input, shared by the refdump and the tests)
- Create: `doc/study/sscdo2/delrm-plugin/gen-ref.sh` (executable)
- Create: `doc/study/sscdo2/delrm-plugin/refdump.cpp`
- Create: `doc/study/sscdo2/delrm-plugin/dsp/leakyint.dsp`, `dsp/comp.dsp`, `dsp/comp2.dsp`, `dsp/delrm.dsp`
- Create (generated): `tests/ref/seam_filters_ref.h`, `tests/ref/seam_compressors_ref.h`, `tests/ref/delrm_ref.h`

**Interfaces:**
- Produces: `struct DelrmSignal { explicit DelrmSignal(double fs); void fill(double* const* in, int n, int channels = 4); }`.
- Produces, in namespace `filtersref`: `kLeaky96[1][16][512]`, `kLeaky96_energy[1][16]`, `kLeaky48[1][16][512]`, `kLeaky48_energy[1][16]`.
- Produces, in namespace `compressorsref`: `kComp96[2][16][512]` (output 0 = the compressed signal, output 1 = the gain in dB), `kComp96_energy[2][16]`, `kComp2_96[2][16][512]`, `kComp2_96_energy[2][16]`.
- Produces, in namespace `delrmref`: `kWin96[4][16][512]`, `kWin96_energy[4][16]`, `kWin48[4][16][512]`, `kWin48_energy[4][16]`, `kChange96[4][16][512]`, `kChange96_energy[4][16]` (mt = 10 from sample 96000).
- Window layout (every reference): the signal runs 2 s in blocks of 256; window w holds the 512 samples starting at sample `w·fs/8`, and `_energy[c][w]` the energy of output c over `[w·fs/8, (w+1)·fs/8)`.

- [ ] **Step 1: Write `tests/delrm_signal.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// delrm_signal.h — the input of the delRM references and tests
//
// A contrabass-clarinet-like low C on each channel: the odd partials 1-9 of
// 29.7 Hz at 1/h, the partial phases shifted per channel, plus LCG noise at
// 0.01 (one generator per channel, seed c+1) and the real chain's DC,
// 3.22e-6. The level steps every 0.5 s through 0.01, 0.05, 0.2, 0.5: below
// the compressor's threshold, across it, and deep into it (the triple
// product is cubic). Included by doc/study/sscdo2/delrm-plugin/refdump.cpp
// and by the tests, so that the Faust render and the C++ render read the
// same samples.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>
#include <cstdint>

struct DelrmSignal {
    explicit DelrmSignal(double fs) : fs_(fs) {
        for (int c = 0; c < 4; ++c) s_[c] = 1u + (uint32_t)c;
    }
    void fill(double* const* in, int n, int channels = 4) {
        static const double kLevel[4] = { 0.01, 0.05, 0.2, 0.5 };
        const double w = 2.0 * 3.141592653589793 * 29.7;
        for (int i = 0; i < n; ++i, ++pos_) {
            const double t = (double)pos_ / fs_;
            int step = (int)(t / 0.5);
            if (step > 3) step = 3;
            for (int c = 0; c < channels; ++c) {
                double x = 0.0;
                for (int h = 1; h <= 9; h += 2) x += std::sin(w * h * t + 0.3 * c * h) / h;
                s_[c] = s_[c] * 1664525u + 1013904223u;
                const double noise = (double)(s_[c] >> 8) / 8388608.0 - 1.0;
                in[c][i] = kLevel[step] * x + 0.01 * noise + 3.22e-6;
            }
        }
    }
private:
    double fs_;
    uint32_t s_[4];
    long pos_ = 0;
};
```

- [ ] **Step 2: Write the four DSP files** under `doc/study/sscdo2/delrm-plugin/dsp/`.

`leakyint.dsp`:
```
// sfi.leakyint(1): the integral in seconds, forgetting below 1 Hz.
import("stdfaust.lib");
sfi = library("seam.filters.lib");
process = sfi.leakyint(1);
```

`comp.dsp`:
```
// co.compressor_mono with delRM's parameters, and its gain in dB. The input
// is scaled by 4 so that the signal's steps cross the threshold.
import("stdfaust.lib");
process = *(4) <: co.compressor_mono(11, -24, 0.03, 0.04),
                  (co.compression_gain_mono(11, -24, 0.03, 0.04) : ba.linear2db);
```

`comp2.dsp`:
```
// The same compressor with other parameters: the library takes them generically.
import("stdfaust.lib");
process = *(4) <: co.compressor_mono(4, -12, 0.005, 0.2),
                  (co.compression_gain_mono(4, -12, 0.005, 0.2) : ba.linear2db);
```

`delrm.dsp`:
```
// The spec: delRM's four channels, one distance (the entry "mt").
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
mt = nentry("mt", 7.291, 0, 30, 0.001);
process = sdt.delrmcomb(mt), (sdt.delrmrm(mt) : sdt.delrmdyn),
          sdt.delrmcomb(mt), (sdt.delrmrm(mt) : sdt.delrmdyn);
```

- [ ] **Step 3: Write `doc/study/sscdo2/delrm-plugin/refdump.cpp`:**

```cpp
// refdump.cpp -- render a delRM reference DSP and print it as C++ arrays,
// doubles in hex-float (exact to the bit). Used by gen-ref.sh only.
//
//   refdump SR SECONDS NAME [AT LABEL VALUE]
//
// The signal (tests/delrm_signal.h) feeds the DSP's inputs (1 or 4) in
// blocks of 256. Window w of output c is the 512 samples from w*SR/8, and
// NAME_energy[c][w] the energy of output c over [w*SR/8, (w+1)*SR/8). With
// AT (a multiple of 256), the entry LABEL is set to VALUE before the block
// starting at sample AT, as the C++ test sets the distance at that block.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#define FAUSTFLOAT double
#include "delrm_signal.h"

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
    void addHorizontalBargraph(const char*, double*, double, double) {}
    void addVerticalBargraph(const char*, double*, double, double) {}
};
struct dsp { virtual ~dsp() {} };
#include "ref.h"

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "see the header of refdump.cpp\n"); return 2; }
    const int sr = atoi(argv[1]);
    const int seconds = atoi(argv[2]);
    const char* name = argv[3];
    const long at = argc >= 7 ? atol(argv[4]) : -1;
    const char* label = argc >= 7 ? argv[5] : nullptr;
    const double value = argc >= 7 ? atof(argv[6]) : 0.0;

    Ref* d = new Ref;
    d->init(sr);
    UI ui; d->buildUserInterface(&ui);
    const int ni = d->getNumInputs(), no = d->getNumOutputs();
    if ((ni != 1 && ni != 4) || no < 1 || no > 4 || (at >= 0 && at % 256 != 0) || sr % 8 != 0) {
        fprintf(stderr, "bad shape, AT or SR\n"); return 2;
    }
    if (at >= 0 && !ui.zones.count(label)) { fprintf(stderr, "no entry %s\n", label); return 2; }

    const int B = 256, W = 512;
    const long period = sr / 8;
    const int nwin = seconds * 8;
    std::vector<double> ib(4 * B), ob(4 * B);
    double* ip[4]; double* op[4];
    for (int c = 0; c < 4; ++c) { ip[c] = ib.data() + c * B; op[c] = ob.data() + c * B; }
    std::vector<double> win((size_t)no * nwin * W, 0.0), en((size_t)no * nwin, 0.0);
    DelrmSignal sig((double)sr);
    const long total = (long)sr * seconds;
    for (long pos = 0; pos < total; pos += B) {
        const int m = (int)(total - pos < B ? total - pos : B);
        if (pos == at) *ui.zones[label] = value;
        sig.fill(ip, m, ni);
        d->compute(m, ip, op);
        for (int c = 0; c < no; ++c)
            for (int k = 0; k < m; ++k) {
                const long g = pos + k; const int w = (int)(g / period); const long off = g % period;
                const double y = op[c][k];
                if (off < W) win[((size_t)c * nwin + w) * W + off] = y;
                en[(size_t)c * nwin + w] += y * y;
            }
    }
    printf("// %s: %d Hz, %d s", name, sr, seconds);
    if (at >= 0) printf(", %s = %g from sample %ld", label, value, at);
    printf("\nstatic const double %s[%d][%d][%d] = {\n", name, no, nwin, W);
    for (int c = 0; c < no; ++c) {
        printf("{");
        for (int w = 0; w < nwin; ++w) {
            printf("{");
            for (int k = 0; k < W; ++k)
                printf("%a%s", win[((size_t)c * nwin + w) * W + k], k + 1 < W ? "," : "");
            printf("}%s", w + 1 < nwin ? "," : "");
        }
        printf("}%s\n", c + 1 < no ? "," : "");
    }
    printf("};\nstatic const double %s_energy[%d][%d] = {\n", name, no, nwin);
    for (int c = 0; c < no; ++c) {
        printf("{");
        for (int w = 0; w < nwin; ++w) printf("%a%s", en[(size_t)c * nwin + w], w + 1 < nwin ? "," : "");
        printf("}%s\n", c + 1 < no ? "," : "");
    }
    printf("};\n");
    return 0;
}
```

- [ ] **Step 4: Write `doc/study/sscdo2/delrm-plugin/gen-ref.sh`** and `chmod +x` it:

```bash
#!/usr/bin/env bash
# gen-ref.sh -- render the Faust references of the delRM plugin and of the
# _common libraries it added (seam_filters.h, seam_compressors.h) into
# tests/ref/. Run by hand when the spec changes; the tests read the
# committed headers and need no faust binary.
#
# FAUSTLIBS: faustlibraries clone (0965ea2 or later)
# SEAMLIBS:  faust-libraries/src (seam.tedesco.lib; h/nextprime.h for sff.np)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
FAUSTLIBS="${FAUSTLIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}"
SEAMLIBS="${SEAMLIBS:-$(cd "$ROOT/../faust-libraries/src" && pwd)}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$ROOT/tests/ref"

build() { # dsp
    faust -I "$FAUSTLIBS" -I "$SEAMLIBS" -double -lang cpp -cn Ref "$HERE/dsp/$1" -o "$WORK/ref.h"
    c++ -std=c++17 -O2 -I "$WORK" -I "$SEAMLIBS/h" -I "$ROOT/tests" "$HERE/refdump.cpp" -o "$WORK/refdump"
}
banner() {
    echo "// GENERATED by doc/study/sscdo2/delrm-plugin/gen-ref.sh -- do not edit."
    echo "// $(faust --version | head -1); faustlibraries $(git -C "$FAUSTLIBS" rev-parse --short HEAD);"
    echo "// faust-libraries $(git -C "$SEAMLIBS" rev-parse --short HEAD)."
    echo "#pragma once"
}

OUT="$ROOT/tests/ref/seam_filters_ref.h"
{ banner; echo "namespace filtersref {"
  build leakyint.dsp
  "$WORK/refdump" 96000 2 kLeaky96
  "$WORK/refdump" 48000 2 kLeaky48
  echo "} // namespace filtersref"; } > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"

OUT="$ROOT/tests/ref/seam_compressors_ref.h"
{ banner; echo "namespace compressorsref {"
  build comp.dsp
  "$WORK/refdump" 96000 2 kComp96
  build comp2.dsp
  "$WORK/refdump" 96000 2 kComp2_96
  echo "} // namespace compressorsref"; } > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"

OUT="$ROOT/tests/ref/delrm_ref.h"
{ banner; echo "namespace delrmref {"
  build delrm.dsp
  "$WORK/refdump" 96000 2 kWin96
  "$WORK/refdump" 48000 2 kWin48
  "$WORK/refdump" 96000 2 kChange96 96000 mt 10
  echo "} // namespace delrmref"; } > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
```

- [ ] **Step 5: Run it.** Run: `doc/study/sscdo2/delrm-plugin/gen-ref.sh`
Expected: three `wrote` lines; `delrm_ref.h` about 2–3 MB. Check by eye: `grep -c "static const" tests/ref/delrm_ref.h` gives 6, `seam_filters_ref.h` 4, `seam_compressors_ref.h` 4.
Sanity: in `kComp96` output 1 (the gain in dB), window 0 is near 0 (below threshold) and window 15 is strongly negative; in `kWin96` channel 0, window 0 sample 0 equals the signal's first sample (the comb with an empty delay).
If `faust` fails on `sff.np`, check that `-I "$SEAMLIBS/h"` reaches `../h/nextprime.h`. If `seam.filters.lib` pulls `seam.lib` and fails on an import, add `-I "$SEAMLIBS"` (already present) and report the message.

- [ ] **Step 6: Commit.**

```bash
git add tests/delrm_signal.h tests/ref/seam_filters_ref.h tests/ref/seam_compressors_ref.h tests/ref/delrm_ref.h doc/study/sscdo2/delrm-plugin
git commit -m "test(delrm): Faust references of sfi.leakyint, co.compressor_mono, sdt.delrm*"
```

---

### Task 4: `seam_filters.h` — the leaky integrator

**Files:**
- Create: `plugins/_common/seam_filters.h`
- Create: `tests/seam_filters_test.cpp`
- Create: `tests/ref_windows.h` (the window comparison shared by Tasks 4–6)
- Modify: `tests/CMakeLists.txt` (append a block)

**Interfaces:**
- Consumes: `filtersref::kLeaky96`, `kLeaky48` (Task 3); `DelrmSignal` (Task 3).
- Produces: `class Seam::LeakyIntegrator { void prepare(double fs, double fc); void reset(); double tick(double x); double pole() const; }`.
- Produces: `refwin::Compare` (below), used by Tasks 5 and 6.

- [ ] **Step 1: Write `tests/ref_windows.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// ref_windows.h — compare a C++ render with a windowed Faust reference
//
// The references hold, per output, the 512 samples from every fs/8 and the
// energy of every fs/8 (doc/study/sscdo2/delrm-plugin/refdump.cpp). A test
// feeds the same signal in the same 256-sample blocks, calls see() for every
// output sample, and reads the largest error relative to the reference's
// peak, and the largest relative error of the energies.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace refwin {

struct Compare {
    Compare(int outputs, int windows, long period)
        : no(outputs), nw(windows), period(period), energy((size_t)outputs * windows, 0.0) {}

    // Output c, global sample g, value y; ref = &kRef[0][0][0], eref = &kRef_energy[0][0].
    void see(int c, long g, double y, const double* ref) {
        const int w = (int)(g / period); const long off = g % period;
        if (w >= nw) return;
        energy[(size_t)c * nw + w] += y * y;
        if (off < 512) {
            const double r = ref[((size_t)c * nw + w) * 512 + off];
            maxErr = std::max(maxErr, std::fabs(y - r));
            peak = std::max(peak, std::fabs(r));
        }
    }
    double relErr() const { return peak > 0.0 ? maxErr / peak : maxErr; }
    double energyRelErr(const double* eref) const {
        double worst = 0.0;
        for (size_t i = 0; i < energy.size(); ++i) {
            const double e = eref[i];
            worst = std::max(worst, e > 0.0 ? std::fabs(energy[i] - e) / e : std::fabs(energy[i]));
        }
        return worst;
    }

    int no, nw; long period;
    std::vector<double> energy;
    double maxErr = 0.0, peak = 0.0;
};

} // namespace refwin
```

- [ ] **Step 2: Write the failing test** `tests/seam_filters_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_filters.h"
#include "delrm_signal.h"
#include "ref_windows.h"
#include "ref/seam_filters_ref.h"
#include <cmath>

using Seam::LeakyIntegrator;

static double runAgainst(double fs, const double* ref, const double* eref, double* energyErr) {
    LeakyIntegrator li;
    li.prepare(fs, 1.0);
    DelrmSignal sig(fs);
    refwin::Compare cmp(1, 16, (long)fs / 8);
    const int B = 256; double buf[B]; double* in[1] = { buf };
    const long total = (long)fs * 2;
    for (long pos = 0; pos < total; pos += B) {
        sig.fill(in, B, 1);
        for (int k = 0; k < B; ++k) cmp.see(0, pos + k, li.tick(buf[k]), ref);
    }
    *energyErr = cmp.energyRelErr(eref);
    return cmp.relErr();
}

TEST_CASE("LeakyIntegrator equals sfi.leakyint(1) at 96 and 48 kHz") {
    double e96, e48;
    const double r96 = runAgainst(96000.0, &filtersref::kLeaky96[0][0][0], &filtersref::kLeaky96_energy[0][0], &e96);
    const double r48 = runAgainst(48000.0, &filtersref::kLeaky48[0][0][0], &filtersref::kLeaky48_energy[0][0], &e48);
    MESSAGE("leakyint rel. error 96k " << r96 << ", 48k " << r48);
    CHECK(r96 < 1e-12);
    CHECK(r48 < 1e-12);
    CHECK(e96 < 1e-10);
    CHECK(e48 < 1e-10);
}

// The amplitude of the integral of sin(2 pi f t) is 1/(2 pi f) in seconds,
// at every rate, above fc.
static double sineGain(double fs, double f) {
    LeakyIntegrator li;
    li.prepare(fs, 1.0);
    const long n = (long)(fs * 12.0);      // 12 s: the 1 Hz pole settles
    double peak = 0.0;
    for (long i = 0; i < n; ++i) {
        const double y = li.tick(std::sin(2.0 * 3.141592653589793 * f * i / fs));
        if (i > n - (long)fs) peak = std::max(peak, std::fabs(y));
    }
    return peak;
}

TEST_CASE("above fc the gain is 1/(2 pi f), the same at 48 and 96 kHz") {
    for (double f : {29.7, 100.0, 1000.0}) {
        const double want = 1.0 / (2.0 * 3.141592653589793 * f);
        const double g96 = sineGain(96000.0, f), g48 = sineGain(48000.0, f);
        CAPTURE(f);
        CHECK(std::fabs(g96 / want - 1.0) < 2e-3);   // the leak's own 1/sqrt(1+(fc/f)^2)
        CHECK(std::fabs(g48 / g96 - 1.0) < 1e-3);
    }
}

TEST_CASE("a DC input stays bounded near 1/(2 pi fc)") {
    LeakyIntegrator li;
    li.prepare(96000.0, 1.0);
    double y = 0.0;
    for (long i = 0; i < 96000L * 10; ++i) y = li.tick(1.0);
    CHECK(std::fabs(y * 2.0 * 3.141592653589793 - 1.0) < 1e-2);
}

TEST_CASE("reset() forgets the state") {
    LeakyIntegrator li;
    li.prepare(96000.0, 1.0);
    for (int i = 0; i < 1000; ++i) li.tick(1.0);
    li.reset();
    CHECK(li.tick(0.0) == 0.0);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# seam_filters.h: sfi.leakyint; references in ref/seam_filters_ref.h
# (doc/study/sscdo2/delrm-plugin/gen-ref.sh).
add_executable(seam_filters_test seam_filters_test.cpp)
target_include_directories(seam_filters_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_filters_test PRIVATE cxx_std_17)
add_test(NAME seam_filters_test COMMAND seam_filters_test)
```

- [ ] **Step 3: Run, see RED.** Run: `cmake -S . -B build-test >/dev/null && cmake --build build-test --config Release --target seam_filters_test 2>&1 | tail -3`
Expected: `'seam_filters.h' file not found`.

- [ ] **Step 4: Implement** `plugins/_common/seam_filters.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_filters.h — the C++ side of seam.filters.lib (sfi)
//
// FAUST REFERENCE (seam.filters.lib):
//   leakyint(fc) = /(ma.SR) : fi.pole(exp(-2*ma.PI*fc/ma.SR));
//
// The leaky integrator, normalised to time: the integral of the input in
// seconds, which forgets below fc. y[n] = x[n]/fs + a*y[n-1], a =
// exp(-2*pi*fc/fs). Above fc its gain is 1/(2*pi*f) at every rate; below fc
// it levels off at about 1/(2*pi*fc), so a DC offset cannot grow without
// bound as it does in fi.integrator. Written in Faust 2026-09-29 for
// SSCDO#2's delRM, whose sdt.delrmint scales it by 96000; the scaling is the
// caller's, not the library's.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cmath>

namespace Seam {

class LeakyIntegrator {
public:
    void prepare(double fs, double fc) {
        invFs_ = 1.0 / fs;
        a_ = std::exp(-2.0 * 3.141592653589793 * fc / fs);
        reset();
    }
    void   reset()       { y_ = 0.0; }
    double pole() const  { return a_; }

    double tick(double x) {
        y_ = x * invFs_ + a_ * y_;
        return y_;
    }

private:
    double invFs_ = 1.0 / 96000.0, a_ = 0.0, y_ = 0.0;
};

} // namespace Seam
```

Faust writes `/(ma.SR)`; the C++ multiplies by `1/fs`. If the first test's error exceeds 1e-12 because of that, replace `x * invFs_` with `x / fs_` (store fs) and record the finding in the log; the tolerance is not to be loosened.

- [ ] **Step 5: Run, see GREEN.** Run: `cmake --build build-test --config Release --target seam_filters_test && ctest --test-dir build-test -C Release -R seam_filters_test -V | grep -E "rel. error|PASS|FAIL"`
Expected: PASS, the relative errors printed (expected near 1e-16).

- [ ] **Step 6: Commit.**

```bash
git add plugins/_common/seam_filters.h tests/seam_filters_test.cpp tests/ref_windows.h tests/CMakeLists.txt
git commit -m "feat(_common): seam_filters.h, the leaky integrator (sfi.leakyint)"
```

---

### Task 5: `seam_compressors.h` — `co.compressor_mono`

**Files:**
- Create: `plugins/_common/seam_compressors.h`
- Create: `tests/seam_compressors_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a block)

**Interfaces:**
- Consumes: `compressorsref::kComp96`, `kComp2_96` (Task 3); `DelrmSignal`, `refwin::Compare` (Tasks 3, 4).
- Produces: `class Seam::CompressorMono { void prepare(double fs, double ratio, double threshDb, double attack, double release); void reset(); double tick(double x); double gainDb() const; }`; `double Seam::tau2pole(double tau, double fs)`.

- [ ] **Step 1: Write the failing test** `tests/seam_compressors_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_compressors.h"
#include "delrm_signal.h"
#include "ref_windows.h"
#include "ref/seam_compressors_ref.h"
#include <cmath>
#include <limits>

using Seam::CompressorMono;

struct Result { double out, gain, energy; };

// The reference DSP: *(4) <: compressor_mono(...), (compression_gain_mono(...) : linear2db).
static Result runAgainst(double ratio, double th, double att, double rel, const double* ref, const double* eref) {
    const double fs = 96000.0;
    CompressorMono c;
    c.prepare(fs, ratio, th, att, rel);
    DelrmSignal sig(fs);
    // Output 0 comes first in the reference's layout: a one-output Compare
    // reads its windows and its 16 energies; the gain is compared sample by
    // sample through a two-output Compare, by its absolute error in dB.
    refwin::Compare out(1, 16, (long)fs / 8), gain(2, 16, (long)fs / 8);
    const int B = 256; double buf[B]; double* in[1] = { buf };
    for (long pos = 0; pos < (long)fs * 2; pos += B) {
        sig.fill(in, B, 1);
        for (int k = 0; k < B; ++k) {
            const double y = c.tick(4.0 * buf[k]);
            out.see(0, pos + k, y, ref);
            gain.see(1, pos + k, c.gainDb(), ref);
        }
    }
    return { out.relErr(), gain.maxErr, out.energyRelErr(eref) };
}

TEST_CASE("CompressorMono equals co.compressor_mono(11, -24, 0.03, 0.04)") {
    const Result r = runAgainst(11, -24, 0.03, 0.04, &compressorsref::kComp96[0][0][0], &compressorsref::kComp96_energy[0][0]);
    MESSAGE("compressor rel. error " << r.out << ", gain abs. error " << r.gain << " dB");
    CHECK(r.out < 1e-12);
    CHECK(r.gain < 1e-9);          // dB, near -40: relative ~1e-11
    CHECK(r.energy < 1e-10);
}

TEST_CASE("the library is generic: another ratio, threshold and times") {
    const Result r = runAgainst(4, -12, 0.005, 0.2, &compressorsref::kComp2_96[0][0][0], &compressorsref::kComp2_96_energy[0][0]);
    CHECK(r.out < 1e-12);
    CHECK(r.gain < 1e-9);
    CHECK(r.energy < 1e-10);
}

TEST_CASE("gainDb() is the gain the sample was multiplied by") {
    CompressorMono c;
    c.prepare(96000.0, 11, -24, 0.03, 0.04);
    for (int i = 0; i < 20000; ++i) {
        const double x = 0.5 * std::sin(i * 0.01);
        const double y = c.tick(x);
        if (std::fabs(x) > 1e-3) CHECK(std::fabs(y / x - std::pow(10.0, c.gainDb() / 20.0)) < 1e-12);
    }
}

TEST_CASE("silence gives exactly 0, never NaN, and the gain returns to 0 dB") {
    CompressorMono c;
    c.prepare(96000.0, 11, -24, 0.03, 0.04);
    for (int i = 0; i < 9600; ++i) c.tick(0.9);
    double y = 1.0;
    for (int i = 0; i < 96000 * 3; ++i) y = c.tick(0.0);
    CHECK(y == 0.0);
    CHECK(std::isfinite(c.gainDb()));
    CHECK(c.gainDb() > -1e-6);
}

TEST_CASE("tau2pole: 0 below epsilon, exp(-1/(tau fs)) otherwise") {
    CHECK(Seam::tau2pole(0.0, 96000.0) == 0.0);
    CHECK(std::fabs(Seam::tau2pole(0.03, 96000.0) - std::exp(-1.0 / (0.03 * 96000.0))) < 1e-16);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# seam_compressors.h: co.compressor_mono; references in
# ref/seam_compressors_ref.h (doc/study/sscdo2/delrm-plugin/gen-ref.sh).
add_executable(seam_compressors_test seam_compressors_test.cpp)
target_include_directories(seam_compressors_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_compressors_test PRIVATE cxx_std_17)
add_test(NAME seam_compressors_test COMMAND seam_compressors_test)
```

- [ ] **Step 2: Run, see RED.** Run: `cmake -S . -B build-test >/dev/null && cmake --build build-test --config Release --target seam_compressors_test 2>&1 | tail -3`
Expected: `'seam_compressors.h' file not found`.

- [ ] **Step 3: Implement** `plugins/_common/seam_compressors.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_compressors.h — the C++ side of compressors.lib (co)
//
// FAUST REFERENCE (compressors.lib, standard, J. O. Smith III, STK-4.3):
//   compressor_mono = compressor_lad_mono(0);
//   compressor_lad_mono(lad,ratio,thresh,att,rel,x)
//     = x@max(0,floor(0.5+ma.SR*lad)) * compression_gain_mono(ratio,thresh,att,rel,x);
//   compression_gain_mono(ratio,thresh,att,rel) =
//     an.amp_follower_ar(att,rel) : ba.linear2db : outminusindb(ratio,thresh) :
//     kneesmooth(att) : ba.db2linear
//   with { kneesmooth(att) = si.smooth(ba.tau2pole(att/2.0));
//          outminusindb(ratio,thresh,level) =
//            max(level-thresh,0.0) * (1.0/max(ma.EPSILON,float(ratio))-1.0); };
//   an.amp_follower_ar(att,rel) = abs : si.onePoleSwitching(att,rel);
//   si.onePoleSwitching(att,rel,x) = loop ~ _ with { loop(y) = (1-c)*x + c*y
//     with { c = ba.if(x > y, ba.tau2pole(att), ba.tau2pole(rel)); }; };
//   ba.tau2pole(tau) = 0 when |tau| < ma.EPSILON, else exp(-1/(tau*ma.SR));
//   ba.linear2db(g) = 20*log10(max(ma.MIN, g));  ba.db2linear(l) = pow(10, l/20);
//
// Two states: the envelope e, and the "knee", a second one-pole on the gain
// in dB with half the attack time. The attack/release switch compares |x|
// with the envelope's PREVIOUS value. gainDb() is the knee's output, the
// gain the sample was just multiplied by: a gain-reduction meter reads it.
// Written 2026-10-02 for SSCDO#2's delRM (ratio 11, -24 dB, 30 ms, 40 ms).
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Seam {

inline double tau2pole(double tau, double fs) {
    return std::fabs(tau) < DBL_EPSILON ? 0.0 : std::exp(-1.0 / (tau * fs));
}

class CompressorMono {
public:
    void prepare(double fs, double ratio, double threshDb, double attack, double release) {
        thresh_ = threshDb;
        slope_ = 1.0 / std::max(DBL_EPSILON, ratio) - 1.0;
        cAtt_ = tau2pole(attack, fs);
        cRel_ = tau2pole(release, fs);
        cKnee_ = tau2pole(attack / 2.0, fs);
        reset();
    }
    void reset() { env_ = 0.0; knee_ = 0.0; }

    double tick(double x) {
        const double a = std::fabs(x);
        const double c = a > env_ ? cAtt_ : cRel_;           // the previous envelope
        env_ = (1.0 - c) * a + c * env_;
        const double level = 20.0 * std::log10(std::max(DBL_MIN, env_));
        const double g = std::max(level - thresh_, 0.0) * slope_;
        knee_ = (1.0 - cKnee_) * g + cKnee_ * knee_;
        return x * std::pow(10.0, knee_ / 20.0);
    }

    double gainDb() const { return knee_; }

private:
    double thresh_ = 0.0, slope_ = 0.0, cAtt_ = 0.0, cRel_ = 0.0, cKnee_ = 0.0;
    double env_ = 0.0, knee_ = 0.0;
};

} // namespace Seam
```

- [ ] **Step 4: Run, see GREEN.** Run: `cmake --build build-test --config Release --target seam_compressors_test && ctest --test-dir build-test -C Release -R seam_compressors_test -V | grep -E "rel. error|PASS|FAIL"`
Expected: PASS. If the output error is above 1e-12, read the generated `ref.h` of `comp.dsp` (rerun the `build` line of `gen-ref.sh` by hand into a scratch folder) and match the order of operations it uses (for instance `si.smooth` written as `x + s*(y - x)`), then record the finding in the log; the tolerance is not to be loosened.

- [ ] **Step 5: Commit.**

```bash
git add plugins/_common/seam_compressors.h tests/seam_compressors_test.cpp tests/CMakeLists.txt
git commit -m "feat(_common): seam_compressors.h, co.compressor_mono with its gain in dB"
```

---

### Task 6: The engine

**Files:**
- Create: `plugins/delrm/source/delrm_dsp.h`
- Create: `tests/delrm_dsp_test.cpp`
- Modify: `tests/CMakeLists.txt` (append a block)

**Interfaces:**
- Consumes: `Seam::PrimeSieve`, `Seam::metresToPrimeSamples` (Task 2), `Seam::IntegerDelay` (Task 2), `Seam::LeakyIntegrator` (Task 4), `Seam::CompressorMono` (Task 5), `Seam::LinearRamp`, `Seam::ScopedNoDenormals`; `delrmref::*`, `DelrmSignal`, `refwin::Compare` (Tasks 3, 4).
- Produces, namespace `delrm`: constants `kChannels = 4`, `kMaxMetres = 30.0`, `kDefaultMetres = 7.291`, `kShortRamp = 0.025`, `kMeterRelease = 0.3`, `kInFloorDb = -70.0`, `kInTopDb = 5.0`, `kGrRangeDb = 48.0`; `uint32_t sieveBound(double fs)`; `uint32_t delayFor(double mt, double fs, const Seam::PrimeSieve&)`; `std::size_t lineLength(double fs, const Seam::PrimeSieve&)`.
- Produces: `class delrm::Engine { bool prepare(double fs); void release(); void reset(); void setDistance(double mt); void setOutput(double); void setPower(bool); template<class T> void process(const T* const* in, T* const* out, int n); uint32_t delaySamples() const; double sampleRate() const; bool prepared() const; std::size_t lineLength() const; double inputPeak(int ch) const; double reductionDb(int k) const; double blockPeak(int ch) const; double blockReductionDb(int k) const; }` — `k` is 0 for channel 2, 1 for channel 4; reductions are depths in dB, ≥ 0.

- [ ] **Step 1: Write the failing test** `tests/delrm_dsp_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_dsp.h"
#include "delrm_signal.h"
#include "ref_windows.h"
#include "ref/delrm_ref.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace delrm;

static void settle(Engine& e, double fs) {
    REQUIRE(e.prepare(fs));
    e.setOutput(1.0); e.setPower(true);
    e.reset();                                  // every ramp onto its target
}

// The signal through the engine in blocks of `block`, 2 s; hook(pos) runs
// before the block starting at pos; every output sample goes to `see`.
template <class Hook, class See>
static void run(Engine& e, double fs, int block, Hook hook, See see, bool inPlace = false) {
    std::vector<double> ib((size_t)4 * block), ob((size_t)4 * block);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + (size_t)c * block; out[c] = inPlace ? in[c] : ob.data() + (size_t)c * block; }
    DelrmSignal sig(fs);
    const long total = (long)fs * 2;
    for (long pos = 0; pos < total; pos += block) {
        const int m = (int)std::min<long>(block, total - pos);
        hook(pos);
        sig.fill(in, m);
        e.process(in, out, m);
        for (int c = 0; c < 4; ++c) for (int k = 0; k < m; ++k) see(c, pos + k, out[c][k]);
    }
}

static refwin::Compare against(double fs, const double* ref, double changeAt = -1, double mt = 0) {
    Engine e; settle(e, fs);
    refwin::Compare cmp(4, 16, (long)fs / 8);
    run(e, fs, 256, [&](long pos) { if (pos == (long)changeAt) e.setDistance(mt); },
        [&](int c, long g, double y) { cmp.see(c, g, y, ref); });
    return cmp;
}

// ── Test 5 of the spec: the four channels against the spec ────────────────
TEST_CASE("the engine equals sdt.delrmcomb and sdt.delrmrm : sdt.delrmdyn at 96 kHz") {
    const auto c = against(96000.0, &delrmref::kWin96[0][0][0]);
    MESSAGE("engine 96k rel. error " << c.relErr());
    CHECK(c.relErr() < 1e-12);
    CHECK(c.energyRelErr(&delrmref::kWin96_energy[0][0]) < 1e-10);
}

TEST_CASE("the engine equals the spec at 48 kHz") {
    const auto c = against(48000.0, &delrmref::kWin48[0][0][0]);
    MESSAGE("engine 48k rel. error " << c.relErr());
    CHECK(c.relErr() < 1e-12);
    CHECK(c.energyRelErr(&delrmref::kWin48_energy[0][0]) < 1e-10);
}

// ── Test 6: a change of distance at the same block in both ────────────────
TEST_CASE("a change of distance at sample 96000 follows the spec through the jump") {
    const auto c = against(96000.0, &delrmref::kChange96[0][0][0], 96000, 10.0);
    CHECK(c.relErr() < 1e-12);
    CHECK(c.energyRelErr(&delrmref::kChange96_energy[0][0]) < 1e-10);
}

// ── Test 4 and 7: the delay and the memory ─────────────────────────────────
TEST_CASE("D at the starting distance is 2113 samples at 96 kHz, 1061 at 48 kHz") {
    Engine e; settle(e, 96000.0);
    CHECK(e.delaySamples() == 2113);
    REQUIRE(e.prepare(48000.0));
    CHECK(e.delaySamples() == 1061);
}

TEST_CASE("every line holds the longest D the slider can ask, at every rate up to 384 kHz") {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0}) {
        const Seam::PrimeSieve s(sieveBound(fs));
        const std::size_t len = lineLength(fs, s);
        long bad = 0, none = 0;
        for (int mm = 0; mm <= 30000; mm += 7) {
            const uint32_t d = delayFor(mm / 1000.0, fs, s);
            if (mm > 1 && d == 0) ++none;            // the sieve ran out
            if ((std::size_t)d + 1 > len) ++bad;
        }
        CAPTURE(fs);
        CHECK(none == 0);
        CHECK(bad == 0);
        CHECK(len == (std::size_t)delayFor(kMaxMetres, fs, s) + 1);
    }
}

TEST_CASE("distances off the range clamp to 0 and 30 m") {
    Engine e; settle(e, 96000.0);
    e.setDistance(-1.0);
    CHECK(e.delaySamples() == 0);
    e.setDistance(31.0);
    const Seam::PrimeSieve s(sieveBound(96000.0));
    CHECK(e.delaySamples() == delayFor(30.0, 96000.0, s));
}

TEST_CASE("at 0 m the comb doubles the input and the triple product is x^2 times the integral") {
    Engine e; settle(e, 96000.0);
    e.setDistance(0.0);
    REQUIRE(e.delaySamples() == 0);
    Seam::LeakyIntegrator li; li.prepare(96000.0, 1.0);
    Seam::CompressorMono cm; cm.prepare(96000.0, 11, -24, 0.03, 0.04);
    double a[64], b[64], y0[64], y1[64], y2[64], y3[64];
    for (int i = 0; i < 64; ++i) { a[i] = 0.1 * std::sin(i * 0.2); b[i] = 0.3 * std::cos(i * 0.07); }
    const double* in[4] = { a, b, a, b };
    double* out[4] = { y0, y1, y2, y3 };
    e.process(in, out, 64);
    double worstComb = 0.0, worstRm = 0.0;
    for (int i = 0; i < 64; ++i) {
        worstComb = std::max(worstComb, std::fabs(y0[i] - 2.0 * a[i]));
        const double p = b[i] * b[i] * (96000.0 * li.tick(b[i]));
        worstRm = std::max(worstRm, std::fabs(y1[i] - cm.tick(10.0 * p)));
    }
    CHECK(worstComb == 0.0);
    CHECK(worstRm == 0.0);     // the same operations in the same order
}

// ── Review Focus: in-place, block sizes, rate change, no memory, silence ───
static std::vector<double> render(double fs, int block, bool inPlace) {
    Engine e; settle(e, fs);
    std::vector<double> all((size_t)4 * (size_t)fs * 2);
    run(e, fs, block, [](long) {}, [&](int c, long g, double y) { all[(size_t)c * (size_t)fs * 2 + (size_t)g] = y; }, inPlace);
    return all;
}

TEST_CASE("in-place buffers give exactly the out-of-place output") {
    CHECK(render(96000.0, 256, true) == render(96000.0, 256, false));
}

TEST_CASE("1, 7 and 4093-sample blocks give exactly the 256-sample output") {
    const auto ref = render(48000.0, 256, false);
    for (int b : {1, 7, 4093}) { CAPTURE(b); CHECK(render(48000.0, b, false) == ref); }
}

TEST_CASE("prepare(48000) after a run at 96 kHz equals a fresh 48 kHz engine") {
    Engine e; settle(e, 96000.0);
    run(e, 96000.0, 256, [](long) {}, [](int, long, double) {});
    REQUIRE(e.prepare(48000.0));
    e.setOutput(1.0); e.setPower(true); e.reset();
    std::vector<double> got((size_t)4 * 96000);
    run(e, 48000.0, 256, [](long) {}, [&](int c, long g, double y) { got[(size_t)c * 96000 + (size_t)g] = y; });
    CHECK(got == render(48000.0, 256, false));
    const Seam::PrimeSieve s(sieveBound(48000.0));
    CHECK(e.lineLength() == lineLength(48000.0, s));
}

TEST_CASE("before prepare and after release, process writes zeros") {
    Engine e;
    double a[32], y[4][32];
    for (double& v : a) v = 0.5;
    const double* in[4] = { a, a, a, a }; double* out[4] = { y[0], y[1], y[2], y[3] };
    for (auto& ch : y) for (double& v : ch) v = 9.0;
    e.process(in, out, 32);
    for (auto& ch : y) for (double v : ch) CHECK(v == 0.0);
    settle(e, 96000.0);
    e.release();
    for (auto& ch : y) for (double& v : ch) v = 9.0;
    e.process(in, out, 32);
    for (auto& ch : y) for (double v : ch) CHECK(v == 0.0);
}

TEST_CASE("silence after the loud part: exact zeros, never NaN, GR back toward 0") {
    Engine e; settle(e, 96000.0);
    run(e, 96000.0, 256, [](long) {}, [](int, long, double) {});
    const double grLoud = e.reductionDb(0);
    std::vector<double> z(256, 0.0), y(4 * 256);
    const double* in[4] = { z.data(), z.data(), z.data(), z.data() };
    double* out[4] = { y.data(), y.data() + 256, y.data() + 512, y.data() + 768 };
    bool finite = true;
    for (int b = 0; b < 96000 * 4 / 256; ++b) {
        e.process(in, out, 256);
        for (double v : y) finite &= std::isfinite(v);
    }
    CHECK(finite);
    for (double v : y) CHECK(v == 0.0);
    CHECK(grLoud > 10.0);
    CHECK(e.reductionDb(0) < 0.01 * grLoud);
}

// ── Test 8: the meters ──────────────────────────────────────────────────────
TEST_CASE("the block meters equal what the engine computed in that block") {
    Engine e; settle(e, 96000.0);
    Seam::LeakyIntegrator li; li.prepare(96000.0, 1.0);
    Seam::CompressorMono cm; cm.prepare(96000.0, 11, -24, 0.03, 0.04);
    Seam::IntegerDelay dl; std::vector<double> buf(e.lineLength(), 0.0); dl.attach(buf.data(), buf.size());
    dl.setDelay(e.delaySamples());
    DelrmSignal sig(96000.0);
    std::vector<double> ib(4 * 256), ob(4 * 256);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * 256; out[c] = ob.data() + c * 256; }
    for (int b = 0; b < 600; ++b) {                    // 1.6 s: through three level steps
        sig.fill(in, 256);
        double peak[4] = {}, depth = 0.0;
        for (int k = 0; k < 256; ++k) {
            for (int c = 0; c < 4; ++c) peak[c] = std::max(peak[c], std::fabs(in[c][k]));
            const double x = in[1][k];
            const double p = dl.tick(x) * x * (96000.0 * li.tick(x));
            cm.tick(10.0 * p);
            depth = std::max(depth, -cm.gainDb());
        }
        e.process(in, out, 256);
        for (int c = 0; c < 4; ++c) REQUIRE(e.blockPeak(c) == peak[c]);
        REQUIRE(e.blockReductionDb(0) == depth);
        REQUIRE(e.inputPeak(0) >= e.blockPeak(0));    // held with release, never below the block
        REQUIRE(e.reductionDb(0) >= e.blockReductionDb(0));
    }
}

TEST_CASE("the held meters release with a 300 ms time constant") {
    Engine e; settle(e, 96000.0);
    double one[256], zero[256], y[4][256];
    std::fill(one, one + 256, 0.5); std::fill(zero, zero + 256, 0.0);
    const double* hi[4] = { one, one, one, one }; const double* lo[4] = { zero, zero, zero, zero };
    double* out[4] = { y[0], y[1], y[2], y[3] };
    e.process(hi, out, 256);
    CHECK(e.inputPeak(0) == 0.5);
    const int blocks = (int)std::lround(0.3 * 96000.0 / 256.0);   // 113 blocks = 0.3013 s
    for (int b = 0; b < blocks; ++b) e.process(lo, out, 256);
    const double want = 0.5 * std::exp(-blocks * 256.0 / (0.3 * 96000.0));
    CHECK(std::fabs(e.inputPeak(0) / want - 1.0) < 1e-9);
}
```

Append to `tests/CMakeLists.txt`:

```cmake
# delrm: the engine against sdt.delrm*; references in ref/delrm_ref.h
# (doc/study/sscdo2/delrm-plugin/gen-ref.sh).
add_executable(delrm_dsp_test delrm_dsp_test.cpp)
target_include_directories(delrm_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(delrm_dsp_test PRIVATE cxx_std_17)
add_test(NAME delrm_dsp_test COMMAND delrm_dsp_test)
```

- [ ] **Step 2: Run, see RED.** Run: `cmake -S . -B build-test >/dev/null && cmake --build build-test --config Release --target delrm_dsp_test 2>&1 | tail -3`
Expected: `'delrm_dsp.h' file not found`.

- [ ] **Step 3: Implement** `plugins/delrm/source/delrm_dsp.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the engine (SDK-free)
//
// delRM of SSCDO#2 on four channels, each processing only its own input.
// Channels 1 and 3: x + x[n-D] (sdt.delrmcomb). Channels 2 and 4: x[n-D]
// times x times its integral anchored at 96 kHz (sdt.delrmrm), times 10,
// into the 11:1 compressor (sdt.delrmdyn). One D for the four channels:
// DDELAY's distance in metres moved to the next prime at the session's rate
// (sma.imt2npsamp). This file only wires the _common blocks.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "seam_compressors.h"
#include "seam_delays.h"
#include "seam_denormals.h"
#include "seam_filters.h"
#include "seam_primes.h"
#include "seam_ramp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace delrm {

constexpr int    kChannels      = 4;
constexpr double kMaxMetres     = 30.0;     // DDELAY's range
constexpr double kDefaultMetres = 7.291;    // Davide's 22 ms
constexpr double kShortRamp     = 0.025;    // s
// sdt.delrmint: sfi.leakyint(1) scaled by the rate SSCDO#2 is played at.
constexpr double kIntegratorFc    = 1.0;
constexpr double kIntegratorScale = 96000.0;
// sdt.delrmdyn: *(10) : co.compressor_mono(11, -24, 0.03, 0.04).
constexpr double kRmGain = 10.0, kRatio = 11.0, kThreshDb = -24.0, kAttack = 0.03, kRelease = 0.04;
// Meters: instant attack per block, one-pole release across blocks.
constexpr double kMeterRelease = 0.3;       // s
constexpr double kInFloorDb = -70.0, kInTopDb = 5.0, kGrRangeDb = 48.0;

// The largest n of 30 m plus room for one prime gap (1024 is above every
// gap below 2^32).
inline uint32_t sieveBound(double fs) {
    return (uint32_t)std::floor(kMaxMetres * fs / Seam::kSpeedOfSoundInterior + 0.5) + 1024u;
}

inline uint32_t delayFor(double mt, double fs, const Seam::PrimeSieve& s) {
    return Seam::metresToPrimeSamples(std::min(kMaxMetres, std::max(0.0, mt)), fs, s);
}

// Rounding and the prime above are non-decreasing, so 30 m asks the longest
// D; +1 holds x[n-D] with x[n]. The spec's 1 << 15 holds 30 m up to 192 kHz
// (17 383) but not at 384 kHz (34 763): exact sizing is right at any rate.
inline std::size_t lineLength(double fs, const Seam::PrimeSieve& s) {
    return (std::size_t)delayFor(kMaxMetres, fs, s) + 1;
}

class Engine {
public:
    // Outside the audio thread (setActive). false when the memory is not
    // there: the engine stays silent.
    bool prepare(double fs) {
        release();
        fs_ = fs;
        try {
            sieve_.reset(new Seam::PrimeSieve(sieveBound(fs)));
            len_ = delrm::lineLength(fs, *sieve_);
            mem_.reset(new double[(std::size_t)kChannels * len_]());
        } catch (const std::bad_alloc&) {
            release();
            return false;
        }
        for (int c = 0; c < kChannels; ++c) dl_[c].attach(mem_.get() + (std::size_t)c * len_, len_);
        for (int r = 0; r < 2; ++r) {
            li_[r].prepare(fs, kIntegratorFc);
            cm_[r].prepare(fs, kRatio, kThreshDb, kAttack, kRelease);
        }
        applyDistance();
        out_.setTarget(out_.target(), kShortRamp, fs_);
        pow_.setTarget(pow_.target(), kShortRamp, fs_);
        for (int c = 0; c < kChannels; ++c) { heldPeak_[c] = blockPeak_[c] = 0.0; }
        for (int r = 0; r < 2; ++r) { heldGr_[r] = blockGr_[r] = 0.0; }
        sampleRate_.store(fs);
        return true;
    }

    void release() {
        mem_.reset();
        sieve_.reset();
        len_ = 0;
        delay_.store(0);
        sampleRate_.store(0.0);
    }

    // Every ramp onto its target (setActive, after the recalled values).
    void reset() { out_.snap(); pow_.snap(); }

    // Audio thread, at the start of a block (or before prepare). The delay
    // jumps, as in the spec.
    void setDistance(double mt) {
        mt = std::min(kMaxMetres, std::max(0.0, mt));
        if (mt == metres_ && applied_) return;
        metres_ = mt;
        if (mem_) applyDistance();
    }
    void setOutput(double v) { if (v != out_.target()) out_.setTarget(v, kShortRamp, fs_); }
    void setPower(bool on) {
        const double t = on ? 1.0 : 0.0;
        if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);
    }

    template <class T>
    void process(const T* const* in, T* const* out, int n) {
        if (!mem_) { zero(out, n); return; }
        // The integrators and the compressors decay in silence: no subnormals.
        Seam::ScopedNoDenormals noDenormals;
        double peak[kChannels] = {}, depth[2] = {};
        for (int k = 0; k < n; ++k) {
            const double g = out_.next() * pow_.next();
            for (int c = 0; c < kChannels; ++c) {
                const double x = (double)in[c][k];
                peak[c] = std::max(peak[c], std::fabs(x));
                const double d = dl_[c].tick(x);
                double y;
                if ((c & 1) == 0) {                                  // channels 1, 3
                    y = x + d;
                } else {                                             // channels 2, 4
                    const int r = c >> 1;
                    const double p = d * x * (kIntegratorScale * li_[r].tick(x));
                    y = cm_[r].tick(kRmGain * p);
                    depth[r] = std::max(depth[r], -cm_[r].gainDb());
                }
                out[c][k] = (T)(y * g);
            }
        }
        const double decay = std::exp(-(double)n / (kMeterRelease * fs_));
        for (int c = 0; c < kChannels; ++c) {
            blockPeak_[c] = peak[c];
            heldPeak_[c] = std::max(peak[c], heldPeak_[c] * decay);
        }
        for (int r = 0; r < 2; ++r) {
            blockGr_[r] = depth[r];
            heldGr_[r] = std::max(depth[r], heldGr_[r] * decay);
        }
    }

    // Readouts. Meters: audio thread (the processor reads them after process).
    bool        prepared() const          { return (bool)mem_; }
    std::size_t lineLength() const        { return len_; }
    uint32_t    delaySamples() const      { return delay_.load(); }
    double      sampleRate() const        { return sampleRate_.load(); }
    double      inputPeak(int c) const    { return heldPeak_[c]; }
    double      reductionDb(int r) const  { return heldGr_[r]; }
    double      blockPeak(int c) const    { return blockPeak_[c]; }
    double      blockReductionDb(int r) const { return blockGr_[r]; }

private:
    void applyDistance() {
        const uint32_t d = delayFor(metres_, fs_, *sieve_);
        for (auto& l : dl_) l.setDelay(d);
        delay_.store(d);
        applied_ = true;
    }

    template <class T>
    static void zero(T* const* out, int n) {
        for (int c = 0; c < kChannels; ++c) std::fill(out[c], out[c] + n, (T)0);
    }

    double fs_ = 96000.0;
    double metres_ = kDefaultMetres;
    bool   applied_ = false;
    std::unique_ptr<double[]> mem_;
    std::size_t len_ = 0;
    std::unique_ptr<Seam::PrimeSieve> sieve_;
    Seam::IntegerDelay    dl_[kChannels];
    Seam::LeakyIntegrator li_[2];
    Seam::CompressorMono  cm_[2];
    Seam::LinearRamp out_, pow_;
    double heldPeak_[kChannels] = {}, blockPeak_[kChannels] = {};
    double heldGr_[2] = {}, blockGr_[2] = {};

    std::atomic<uint32_t> delay_{0};
    std::atomic<double>   sampleRate_{0.0};
};

} // namespace delrm
```

Notes for the implementer:
- `IntegerDelay::attach` zeroes the line; `prepare` already allocated it zero-filled, so this is a second pass over ~1 MiB outside the audio thread, accepted.
- In the "prepare(48000) after a run" test, `prepare` must reset the integrators and compressors (their `prepare` calls `reset()`): check that the test passes for that reason.
- The ramps are `snap()`ped by the test's `reset()`; with output and power at 1 the gain is exactly 1.0, so the engine equals the spec without a ramp factor.

- [ ] **Step 4: Run, see GREEN.** Run: `cmake --build build-test --config Release --target delrm_dsp_test && ctest --test-dir build-test -C Release -R delrm_dsp_test -V | grep -E "rel. error|test cases|FAIL"`
Expected: every test case passes; the two relative errors printed (expected 1e-16 to 1e-14). Record them in the log.

- [ ] **Step 5: Commit.**

```bash
git add plugins/delrm/source/delrm_dsp.h tests/delrm_dsp_test.cpp tests/CMakeLists.txt
git commit -m "feat(delrm): the engine, equal to sdt.delrm* at 96 and 48 kHz, with block meters"
```

---

### Task 7: Parameters and state

**Files:**
- Create: `plugins/delrm/source/delrm_params.h`, `plugins/delrm/source/delrm_state.h`
- Create: `tests/delrm_params_test.cpp`, `tests/delrm_state_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `delrm::Engine`, `kMaxMetres`, `kDefaultMetres` (Task 6).
- Produces: `enum class delrm::Param : int { Power = 0, Distance, Output }`, `kNumParams = 3`, `double distanceToNormalized(double mt)`, `double normalizedToDistance(double norm)`, `double defaultNormalized(Param)`, `struct Plain { bool power; double metres, output; }`, `class ParamBox { void store(Param, double); double normalized(Param) const; Plain plain() const; }`, `void applyTo(const Plain&, Engine&)`; `void writeState(IBStream*, const ParamBox&)`, `int readState(IBStream*, ParamBox&)`.

- [ ] **Step 1: Write the failing tests.** `tests/delrm_params_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_params.h"
#include <cmath>

using namespace delrm;

TEST_CASE("the defaults: power off, 7.291 m, output 0") {
    ParamBox b;
    const Plain p = b.plain();
    CHECK_FALSE(p.power);
    CHECK(std::fabs(p.metres - 7.291) < 1e-12);
    CHECK(p.output == 0.0);
}

TEST_CASE("the distance maps linearly over 0-30 m and round-trips") {
    for (double mt : {0.0, 0.001, 7.291, 15.0, 30.0})
        CHECK(std::fabs(normalizedToDistance(distanceToNormalized(mt)) - mt) < 1e-12);
}

TEST_CASE("host values off the range clamp") {
    ParamBox b;
    b.store(Param::Distance, -0.2);
    CHECK(b.plain().metres == 0.0);
    b.store(Param::Distance, 1.3);
    CHECK(b.plain().metres == 30.0);
    b.store(Param::Output, 1.7);
    CHECK(b.plain().output == 1.0);
}

TEST_CASE("applyTo sets the engine's distance, output and power") {
    Engine e;
    REQUIRE(e.prepare(96000.0));
    ParamBox b;
    b.store(Param::Distance, distanceToNormalized(10.0));
    applyTo(b.plain(), e);
    const Seam::PrimeSieve s(sieveBound(96000.0));
    CHECK(e.delaySamples() == delayFor(10.0, 96000.0, s));
}
```

`tests/delrm_state_test.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace delrm;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the processor's state round-trips every parameter") {
    ParamBox a;
    a.store(Param::Power, 1.0);
    a.store(Param::Distance, distanceToNormalized(12.345));
    a.store(Param::Output, 0.8);
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
        w.writeDouble(1.0);                              // Power only
    }
    rewindStream(stream);
    ParamBox b;
    b.store(Param::Distance, 0.9);                       // the recall must replace it by the default
    CHECK(readState(&stream, b) == 1);
    CHECK(b.plain().power);
    CHECK(std::fabs(b.plain().metres - kDefaultMetres) < 1e-12);
}
```

Append to `tests/CMakeLists.txt` (the state test links the SDK base, as `stunedrev_state_test` does):

```cmake
add_executable(delrm_params_test delrm_params_test.cpp)
target_include_directories(delrm_params_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(delrm_params_test PRIVATE cxx_std_17)
add_test(NAME delrm_params_test COMMAND delrm_params_test)

add_executable(delrm_state_test
    delrm_state_test.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
target_include_directories(delrm_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${vst3sdk_SOURCE_DIR}
)
target_link_libraries(delrm_state_test PRIVATE base sdk_common pluginterfaces)
if(APPLE)
    target_link_libraries(delrm_state_test PRIVATE ${CORE_FOUNDATION})
endif()
target_compile_features(delrm_state_test PRIVATE cxx_std_17)
add_test(NAME delrm_state_test COMMAND delrm_state_test)
```

(`CORE_FOUNDATION` is already found by the stunedrev block above.)

- [ ] **Step 2: Run, see RED.** Run: `cmake -S . -B build-test >/dev/null && cmake --build build-test --config Release --target delrm_params_test delrm_state_test 2>&1 | grep -m2 error`
Expected: `'delrm_params.h' file not found`.

- [ ] **Step 3: Implement** `plugins/delrm/source/delrm_params.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the parameters between the threads (SDK-free)
//
// The three controls live here as normalized values in atomics, as in LMO
// and stunedrev: process() stores what the host's queues bring and reads the
// box; setState stores a recalled preset from the UI thread. Neither touches
// the SDK's Parameter objects from the audio thread.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_dsp.h"
#include <algorithm>
#include <atomic>

namespace delrm {

// The state's order on disk (append-only, never reordered).
enum class Param : int { Power = 0, Distance, Output };
constexpr int kNumParams = 3;

inline double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }
inline double distanceToNormalized(double mt) { return clamp01(mt / kMaxMetres); }
inline double normalizedToDistance(double n)  { return clamp01(n) * kMaxMetres; }

inline double defaultNormalized(Param p) {
    return p == Param::Distance ? distanceToNormalized(kDefaultMetres) : 0.0;
}

struct Plain {
    bool   power;
    double metres, output;
};

class ParamBox {
public:
    ParamBox() { for (int i = 0; i < kNumParams; ++i) v_[i].store(defaultNormalized((Param)i)); }
    void   store(Param p, double norm) { v_[(int)p].store(norm); }
    double normalized(Param p) const   { return v_[(int)p].load(); }

    Plain plain() const {
        Plain x;
        x.power  = normalized(Param::Power) >= 0.5;
        x.metres = normalizedToDistance(normalized(Param::Distance));
        x.output = clamp01(normalized(Param::Output));
        return x;
    }

private:
    std::atomic<double> v_[kNumParams];
};

inline void applyTo(const Plain& p, Engine& e) {
    e.setDistance(p.metres);
    e.setOutput(p.output);
    e.setPower(p.power);
}

} // namespace delrm
```

`plugins/delrm/source/delrm_state.h`:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the processor's state (SDK)
//
// Three normalized doubles in Param order, little-endian, under the suite's
// append-only contract (seam_state.h): a short blob keeps the defaults for
// the fields it lacks. The meters are not state.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_params.h"
#include "seam_state.h"
#include "base/source/fstreamer.h"

namespace delrm {

inline void writeState(Steinberg::IBStream* state, const ParamBox& box) {
    Steinberg::IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumParams; ++i) s.writeDouble(box.normalized((Param)i));
}

inline int readState(Steinberg::IBStream* state, ParamBox& box) {
    double v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = defaultNormalized((Param)i);
    const int n = Seam::readStateDoubles(state, v, kNumParams);
    for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);
    return n;
}

} // namespace delrm
```

- [ ] **Step 4: Run, see GREEN.** Run: `cmake --build build-test --config Release --target delrm_params_test delrm_state_test && ctest --test-dir build-test -C Release -R "delrm_(params|state)_test"`
Expected: both PASS.

- [ ] **Step 5: Commit.**

```bash
git add plugins/delrm/source/delrm_params.h plugins/delrm/source/delrm_state.h tests/delrm_params_test.cpp tests/delrm_state_test.cpp tests/CMakeLists.txt
git commit -m "feat(delrm): ParamBox of atomics and the append-only state"
```

---

### Task 8: The processor, the footer, the window, the build

**Files:**
- Create: `plugins/delrm/CMakeLists.txt`, `plugins/delrm/source/delrm_ids.h`, `plugins/delrm/source/delrm_processor.h`, `plugins/delrm/source/delrm_processor.cpp`, `plugins/delrm/source/delrm_views.h`, `plugins/delrm/source/version.h`, `plugins/delrm/resource/delrm.uidesc`
- Modify: `CMakeLists.txt:130` (add `add_subdirectory(plugins/delrm)` after stunedrev)

**Interfaces:**
- Consumes: everything of Tasks 6–7.
- Produces: the `delrm.vst3` bundle; parameter IDs `kParamPower = 100`, `kParamDistance = 101`, `kParamOutput = 102`, meters `kParamIn1 = 200` … `kParamIn4 = 203`, `kParamGr2 = 204`, `kParamGr4 = 205`.

- [ ] **Step 1: `delrm_ids.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — unique identifier + parameter IDs
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// 19th plugin in the suite. word3 = ASCII "DRM\0".
static const Steinberg::FUID DelrmProcessorUID (0x5E4D0012, 0xA1B2C3D4, 0x44524D00, 0x00000012);

enum DelrmParams : Steinberg::Vst::ParamID {
    kParamPower    = 100,   // off / on        (100 + delrm::Param index)
    kParamDistance = 101,   // m, 0-30
    kParamOutput   = 102,   // linear, CC82 in the original
    // Read-only meters.
    kParamIn1 = 200, kParamIn2 = 201, kParamIn3 = 202, kParamIn4 = 203,   // input peak, dB
    kParamGr2 = 204, kParamGr4 = 205                                      // reduction, dB below
};

} // namespace Seam
```

- [ ] **Step 2: `version.h`** (copy of stunedrev's with `delrm.vst3` and the description `SEAM DELRM – SSCDO#2 comb and triple product`):

```cpp
//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — Version and metadata
//─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "pluginterfaces/base/fplatform.h"
#include "projectversion.h"

#define stringOriginalFilename  "delrm.vst3"
#if SMTG_PLATFORM_64
#define stringFileDescription   "SEAM DELRM – SSCDO#2 comb and triple product (64Bit)"
#else
#define stringFileDescription   "SEAM DELRM – SSCDO#2 comb and triple product"
#endif
#define stringCompanyWeb        "https://s-e-a-m.github.io"
#define stringCompanyEmail      "mailto:seam@example.com"
#define stringCompanyName       "SEAM"
#define stringLegalCopyright    "© 2026 Giuseppe Silvi – GPL-3.0"
#define stringLegalTrademarks   ""
```

- [ ] **Step 3: `CMakeLists.txt`** (plugin): stunedrev's, with `stunedrev` → `delrm` everywhere, the description `"SEAM DELRM – SSCDO#2 comb and triple product"`, the source list

```cmake
set(delrm_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/delrm_ids.h
    source/delrm_dsp.h
    source/delrm_params.h
    source/delrm_state.h
    source/delrm_views.h
    source/delrm_processor.cpp
    source/delrm_processor.h
    source/version.h
    resource/delrm.uidesc
)
```

and `BUNDLE_IDENTIFIER "io.github.s-e-a-m.delrm"`. Add `add_subdirectory(plugins/delrm)` to the root `CMakeLists.txt` after `add_subdirectory(plugins/stunedrev)`.

- [ ] **Step 4: `delrm_processor.h`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco)
//
// delRM (delRM_duet): four channels, each processing only its own input
// from the TETRAREC. Channels 1 and 3 are a feed-forward comb, x + x[n-D];
// channels 2 and 4 multiply x[n-D], x and its integral, and pass the product
// through an 11:1 compressor that works as a limiter near -20 dBFS.
//
// FAUST REFERENCE (seam.tedesco.lib, seam.filters.lib, seam.math.lib,
// compressors.lib):
//
//   imt2npsamp(mt) = select2(n < 2, n : sff.np, n)
//                    with { mm = floor(mt*1000 + 0.5)/1000;
//                           n  = int(floor(mm*ma.SR/isos + 0.5)); };  isos = 331.4
//   leakyint(fc)   = /(ma.SR) : fi.pole(exp(-2*ma.PI*fc/ma.SR));
//   delrmcomb(mt)  = fi.ff_comb(1 << 15, sma.imt2npsamp(mt), 1, 1);
//   delrmint       = sfi.leakyint(1) : *(96000);
//   delrmrm(mt)    = _ <: de.delay(1 << 15, sma.imt2npsamp(mt)), _, delrmint : *, _ : *;
//   delrmdyn       = *(10) : co.compressor_mono(11, -24, 0.03, 0.04);
//   process        = delrmcomb(mt), (delrmrm(mt) : delrmdyn),
//                    delrmcomb(mt), (delrmrm(mt) : delrmdyn);
//
// Re-implemented by hand (seam-ltm convention) in delrm_dsp.h, on the
// reusable libraries of _common: seam_delays.h (de.delay), seam_filters.h
// (sfi.leakyint), seam_compressors.h (co.compressor_mono), seam_primes.h
// (sma.imt2npsamp through a sieve) and seam_ramp.h. The delay lines are
// sized exactly for 30 m at the session's rate: the spec's 1 << 15 holds
// 30 m up to 192 kHz, not at 384 kHz.
//
// SR rule of the SSCDO#2 port: D is a distance, so a time at every rate,
// with each rate's prime; the integrator is anchored at 96 kHz; the
// compressor is in seconds. delRM sounds at 48 kHz as at 96 kHz.
//
// Against the original (decided with Davide, 2026-10-02): no DC blockers
// (the rehearsal's listening judges the thinner bass), one output fader
// (CC82) in place of Pd's gain and the .dsp's internal 0.9, POWER, 25 ms
// ramps, the input meters and the gain reduction of channels 2 and 4.
//
// Studies and decisions: doc/study/sscdo2/ (delrm-*), logs/2026-10-02-sscdo2-delrm.md.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "delrm_params.h"

namespace Seam {

class DelrmProcessor : public Steinberg::Vst::SingleComponentEffect,
                       public VSTGUI::VST3EditorDelegate {
public:
    DelrmProcessor() = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new DelrmProcessor);
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

    // VST3EditorDelegate: the footer line with D.
    VSTGUI::CView* PLUGIN_API createCustomView(
        VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

private:
    double sampleRate() const {
        return processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 96000.0;
    }
    void publishMeters(Steinberg::Vst::ProcessData& data);

    delrm::Engine   engine_;
    delrm::ParamBox box_;
};

} // namespace Seam
```

- [ ] **Step 5: `delrm_views.h`** — the footer line:

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the footer view (VSTGUI)
//
// One line: D in milliseconds and in samples at the session's rate, read
// from the engine's atomics by a GUI timer, as stunedrev's centroids. The
// prime changing with the rate is visible here.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "delrm_dsp.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"
#include <cstdio>

namespace Seam {

class DelrmFooter : public VSTGUI::CView {
public:
    DelrmFooter(const VSTGUI::CRect& size, const delrm::Engine* engine,
                VSTGUI::CFontRef font, const VSTGUI::CColor& color)
        : CView(size), engine_(engine), font_(font), color_(color) {
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) { invalid(); }, 200, true);
    }
    ~DelrmFooter() override { if (timer_) timer_->stop(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        char line[128];
        const double fs = engine_->sampleRate();
        if (fs > 0.0) {
            const unsigned d = engine_->delaySamples();
            std::snprintf(line, sizeof line, "D  %.2f ms \xC2\xB7 %u samples @ %.1f kHz",
                          1000.0 * d / fs, d, fs / 1000.0);
        } else {
            std::snprintf(line, sizeof line, "D  \xE2\x80\x94  inactive");
        }
        c->setFont(font_);
        c->setFontColor(color_);
        c->drawString(line, getViewSize(), kLeftText);
        setDirty(false);
    }

private:
    const delrm::Engine* engine_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    VSTGUI::CColor color_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

} // namespace Seam
```

Check the includes against `plugins/stunedrev/source/stunedrev_views.h` and copy its include lines if they differ.

- [ ] **Step 6: `delrm_processor.cpp`:**

```cpp
//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — Implementation
//──────────────────────────────────────────────────────────────────────────
#include "delrm_processor.h"
#include "delrm_ids.h"
#include "delrm_state.h"
#include "delrm_views.h"
#include "seam_meter.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace Seam {
using namespace Steinberg;
using namespace Steinberg::Vst;

static ParamID idOf(delrm::Param p) { return kParamPower + (ParamID)p; }

// A gain reduction shown as a negative dB value, while its normalized value
// grows with the depth: the bar, drawn right to left, grows as the
// compressor works.
class ReductionParameter : public RangeParameter {
public:
    ReductionParameter(const TChar* title, ParamID id)
        : RangeParameter(title, id, STR16("dB"), 0.0, delrm::kGrRangeDb, 0.0, 0, ParameterInfo::kIsReadOnly) {}
    void toString(ParamValue norm, String128 string) const SMTG_OVERRIDE {
        const double db = toPlain(norm);
        char s[32];
        std::snprintf(s, sizeof s, db >= 0.05 ? "-%.1f" : "0.0", db);
        UString(string, 128).fromAscii(s);
    }
};

tresult PLUGIN_API DelrmProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Four channels in the original's order (dac~ 9 10 11 12): comb, triple
    // product, comb, triple product.
    addAudioInput (STR16("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    using namespace delrm;
    parameters.addParameter(new RangeParameter(
        STR16("Power"), kParamPower, STR16(""),
        0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate | ParameterInfo::kIsList));

    auto* dist = new RangeParameter(STR16("Distance"), kParamDistance, STR16("m"),
        0.0, kMaxMetres, kDefaultMetres, 0, ParameterInfo::kCanAutomate);
    dist->setPrecision(3);
    parameters.addParameter(dist);

    auto* out = new RangeParameter(STR16("Output"), kParamOutput, STR16(""),
        0.0, 1.0, 0.0, 0, ParameterInfo::kCanAutomate);
    out->setPrecision(3);
    parameters.addParameter(out);

    const char16* inNames[kChannels] = { STR16("in 1"), STR16("in 2"), STR16("in 3"), STR16("in 4") };
    for (int c = 0; c < kChannels; ++c) {
        auto* m = new RangeParameter(inNames[c], kParamIn1 + c, STR16("dB"),
            kInFloorDb, kInTopDb, kInFloorDb, 0, ParameterInfo::kIsReadOnly);
        m->setPrecision(1);
        parameters.addParameter(m);
    }
    parameters.addParameter(new ReductionParameter(STR16("GR 2"), kParamGr2));
    parameters.addParameter(new ReductionParameter(STR16("GR 4"), kParamGr4));
    return kResultOk;
}

tresult PLUGIN_API DelrmProcessor::terminate() {
    engine_.release();
    return SingleComponentEffect::terminate();
}

// The delay lines are allocated and zeroed here, never in process().
tresult PLUGIN_API DelrmProcessor::setActive(TBool state) {
    if (state) {
        engine_.prepare(sampleRate());
        delrm::applyTo(box_.plain(), engine_);   // recalled values become the targets...
        engine_.reset();                         // ...and the ramps start on them
    } else {
        engine_.release();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API DelrmProcessor::process(ProcessData& data) {
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
                box_.store((delrm::Param)(id - kParamPower), v);
        }
    }
    delrm::applyTo(box_.plain(), engine_);

    if (data.numOutputs > 0 && data.numSamples > 0) {
        data.outputs[0].silenceFlags = 0;
        void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);
        const bool shaped = data.numInputs > 0 &&
            data.inputs[0].numChannels >= delrm::kChannels &&
            data.outputs[0].numChannels >= delrm::kChannels;
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
    publishMeters(data);
    return kResultOk;
}

// Read-only parameters, one point per block (the dslar idiom).
void DelrmProcessor::publishMeters(ProcessData& data) {
    auto* oc = data.outputParameterChanges;
    if (!oc) return;
    auto put = [oc](ParamID id, double norm) {
        int32 idx = 0;
        if (auto* q = oc->addParameterData(id, idx)) { int32 o = 0; q->addPoint(0, norm, o); }
    };
    using namespace delrm;
    const double span = kInTopDb - kInFloorDb;
    for (int c = 0; c < kChannels; ++c) {
        const double db = seam::meter::lin2db(engine_.inputPeak(c), kInFloorDb);
        put(kParamIn1 + c, std::min(1.0, std::max(0.0, (db - kInFloorDb) / span)));
    }
    put(kParamGr2, std::min(1.0, engine_.reductionDb(0) / kGrRangeDb));
    put(kParamGr4, std::min(1.0, engine_.reductionDb(1) / kGrRangeDb));
}

tresult PLUGIN_API DelrmProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API DelrmProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    delrm::readState(state, box_);
    for (int i = 0; i < delrm::kNumParams; ++i)
        setParamNormalized(idOf((delrm::Param)i), box_.normalized((delrm::Param)i));
    return kResultOk;
}

tresult PLUGIN_API DelrmProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    delrm::writeState(state, box_);
    return kResultOk;
}

tresult PLUGIN_API DelrmProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn, SpeakerArrangement* out, int32 numOut) {
    if (numIn == 1 && numOut == 1 &&
        SpeakerArr::getChannelCount(in[0])  == 4 &&
        SpeakerArr::getChannelCount(out[0]) == 4)
        return SingleComponentEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

IPlugView* PLUGIN_API DelrmProcessor::createView(FIDString name) {
    if (name && FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "view", "delrm.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API DelrmProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes&,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor*) {
    if (!name || std::string(name) != "DelrmFooter") return nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor;
    if (description) description->getColor("TextLight", text);
    VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
    if (!font) font = VSTGUI::kNormalFontSmall;
    return new DelrmFooter(VSTGUI::CRect(0, 0, 260, 16), &engine_, font, text);
}

} // namespace Seam

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(
        INLINE_UID_FROM_FUID(Seam::DelrmProcessorUID),
        Steinberg::PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM DELRM",
        0,
        "Fx|Delay",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::DelrmProcessor::createInstance)
END_FACTORY
```

- [ ] **Step 7: The window.** Copy `plugins/_template/resource/_template.uidesc` to `plugins/delrm/resource/delrm.uidesc` and read both it and `plugins/lmo/resource/lmo.uidesc` (format S). Then fill it as follows, keeping the template's zone comments and palette:
  - size `300, 510` (min and max equal);
  - HEADER: title `SEAM DELRM`, subtitle `Studio sul Corpo d'Ombra #2`, info line `comb and triple product · four channels`;
  - OPS: POWER checkbox and caption at the LMO positions (y = 100);
  - FINE: `distance (m)` block at y = 126/142/162 (CTextEdit `value-precision="3"`), `output` block at y = 184/200/220 (`value-precision="3"`);
  - FOOTER:
    - the D line: `<view class="CView" custom-view-name="DelrmFooter" origin="20, 252" size="260, 16"/>` (the processor's `createCustomView` builds it);
    - six meter rows from y = 276 on a 18 px stride (276, 294, 312, 330, then a 6 px gap, 354, 372). Each row: a `CTextLabel` `origin="20, y" size="40, 14"` in `InfoFont` (`in 1` … `in 4`, `GR 2`, `GR 4`); a `CSlider` `origin="64, y+2" size="160, 10"` bound to the meter's control tag, `draw-back="true" draw-back-color="SliderTrack" draw-value="true" draw-value-color="MeterFill" draw-frame="true" draw-frame-color="SliderTrack" frame-width="1" mode="free click"`, and for the two GR rows also `reverse-orientation="true"`; a `CParamDisplay` `origin="228, y" size="52, 14"` in `InfoFont`, `text-alignment="right"`, `value-precision="1"`, `transparent="true"`;
    - the SEAM logo at `origin="30, 404" size="240, 77"`;
  - colors: add `<color name="MeterFill" rgba="#c8a24aff"/>` to the palette if the template lacks it;
  - control tags: `Power 100`, `Distance 101`, `Output 102`, `In1 200`, `In2 201`, `In3 202`, `In4 203`, `Gr2 204`, `Gr4 205`.

Check `ui-style.md` for the FOOTER's order (runtime feedback, then the logo) and adjust y values so nothing overlaps; the window height follows the logo (`404 + 77 + 18` ≈ 500, round to 510 if the template's bottom margin asks it).

- [ ] **Step 8: Build.** Run: `cmake --build build --config Release --target delrm 2>&1 | tail -5`
Expected: `** BUILD SUCCEEDED **`, and `~/Library/Audio/Plug-Ins/VST3/delrm.vst3` points to this tree (see the VST3 symlink memory). If `build` is not configured, configure it as for stunedrev: `cmake -S . -B build -G Xcode -DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.

- [ ] **Step 9: Validate and lint.**
Run: `build/bin/Release/validator ~/Library/Audio/Plug-Ins/VST3/delrm.vst3 2>&1 | tail -3` (find the validator path as stunedrev's log records it if this one differs)
Expected: `47 tests passed, 0 tests failed`.
Run: `python3 tools/check-uidesc.py`
Expected: no ERROR; one WARN for the missing `docs/img/delrm.png` (Task 10).
Run: `ctest --test-dir build-test -C Release 2>&1 | tail -3`
Expected: every test passes (the lint runs as a ctest too).

- [ ] **Step 10: Commit.**

```bash
git add CMakeLists.txt plugins/delrm
git commit -m "feat(delrm): the processor, the footer, the S window with input and GR meters"
```

---

### Task 9: Mutations and CPU

**Files:**
- Create: `doc/study/sscdo2/delrm-plugin/mutations.md`
- Create: `doc/study/sscdo2/delrm-plugin/cpu.cpp`

- [ ] **Step 1: Apply each mutation, rebuild the named test, record RED or GREEN, restore the source** (`git diff` empty after each). The mutations:

| test | mutation |
|---|---|
| seam_primes_test: metres | `kSpeedOfSoundInterior = 343.0` |
| seam_primes_test: metres | no millimetre rounding (`mm = mt`) |
| seam_delays_test | `r = pos_ + len_ - d_ + 1` (one sample short) |
| seam_delays_test | read before write (`d = 0` returns the oldest sample) |
| seam_filters_test | `a_ = std::exp(-2.0 * 3.141592653589793 * fc / fs)` with `fc * 2` |
| seam_filters_test | `x * invFs_` replaced by `x` (a sum of samples) |
| seam_compressors_test | the knee removed (`knee_ = g`) |
| seam_compressors_test | the switch compares with the updated envelope: `const double c = a > ((1.0 - cAtt_) * a + cAtt_ * env_) ? cAtt_ : cRel_;` |
| seam_compressors_test | `slope_ = 1.0 / ratio` (sign lost) |
| seam_compressors_test: generic | `cKnee_ = tau2pole(attack, fs)` (no /2) |
| delrm_dsp_test: engine 96/48 | `kIntegratorScale = 48000.0` |
| delrm_dsp_test: engine 96/48 | channels swapped (`(c & 1) == 1` for the comb) |
| delrm_dsp_test: engine 96/48 | `kRmGain = 1.0` |
| delrm_dsp_test: change | `setDistance` stores `metres_` but `applyDistance` runs only in `prepare` |
| delrm_dsp_test: memory | `lineLength` without `+ 1` |
| delrm_dsp_test: rate change | `prepare` does not call `li_[r].prepare` (the old pole and state survive) |
| delrm_dsp_test: in-place | `peak[c]` read from `out[c][k]` after the write |
| delrm_dsp_test: meters | the block depth taken from the last sample, not the deepest |
| delrm_dsp_test: release | `decay = std::exp(-1.0 / (kMeterRelease * fs_))` (per block, not per sample) |
| delrm_dsp_test: silence | `ScopedNoDenormals` removed (expect GREEN in output values: document as equivalent for correctness, the CPU check of Step 2 covers it) |
| delrm_params_test | `normalizedToDistance` without the clamp |
| delrm_state_test | `readState` does not store the fields it read |

Write `mutations.md` in the format of `doc/study/sscdo2/stunedrev-plugin/mutations.md`: a title, two sentences on the method, the table with a result column. A mutation that stays GREEN is either an equivalent mutant (explain why) or a hole: in that case, write the missing test in the owning task's test file, see it RED, and record both.

- [ ] **Step 2: CPU.** `doc/study/sscdo2/delrm-plugin/cpu.cpp`:

```cpp
// cpu.cpp -- the cost of the delRM engine: percent of a core at 96 and
// 192 kHz, 256-sample blocks, the test signal then 60 s of silence (the
// silent tail is where subnormals would grow).
// c++ -std=c++17 -O3 -I ../../../../plugins/delrm/source -I ../../../../plugins/_common -I ../../../../tests cpu.cpp -o cpu
#include "delrm_dsp.h"
#include "delrm_signal.h"
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    for (double fs : {96000.0, 192000.0}) {
        delrm::Engine e;
        if (!e.prepare(fs)) { printf("allocation failed at %g\n", fs); continue; }
        e.setOutput(1.0); e.setPower(true); e.reset();
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
        DelrmSignal sig(fs);
        auto time = [&](double seconds, bool silent) {
            const long blocks = (long)(seconds * fs / B);
            const auto t0 = std::chrono::steady_clock::now();
            for (long b = 0; b < blocks; ++b) {
                if (silent) std::fill(ib.begin(), ib.end(), 0.0); else sig.fill(in, B);
                e.process(in, out, B);
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / seconds;
        };
        const double sound = time(20.0, false), silence = time(60.0, true);
        printf("%6.0f Hz: %.2f %% of a core on sound, %.2f %% on silence, lines %.2f MiB\n",
               fs, 100.0 * sound, 100.0 * silence, 4.0 * e.lineLength() * sizeof(double) / 1048576.0);
    }
}
```

Run it from its folder with the compile line in its header. Record the output in the plugin README and in `doc/study/sscdo2/delrm-plugin/README.md` (Task 10). If the silence figure is clearly above the sound figure, `ScopedNoDenormals` is not taking effect: stop and report.

- [ ] **Step 3: Commit.**

```bash
git add doc/study/sscdo2/delrm-plugin/mutations.md doc/study/sscdo2/delrm-plugin/cpu.cpp tests
git commit -m "test(delrm): every test verified by mutation; CPU measured"
```

---

### Task 10: Documentation, registry, report numbers, host check

**Files:**
- Create: `plugins/delrm/doc/README.md`, `doc/study/sscdo2/delrm-plugin/README.md`
- Modify: `doc/study/sscdo2/README.md` (table row), `doc/plugins.toml`, `doc/scripts/test-doc.sh:21,35,38`, `doc/scripts/render-readme.py:31`, `CLAUDE.md:90,92`, `logs/2026-10-02-sscdo2-delrm.md`, `doc/study/sscdo2/report/parte2-delrm.tex`
- Create (from Giuseppe): `docs/img/delrm.png`

- [ ] **Step 1: `plugins/delrm/doc/README.md`**, on the model of `plugins/stunedrev/doc/README.md`, one sentence per line:
  - what delRM is (four channels, each its own input; comb on 1 and 3, triple product and compressor on 2 and 4; outputs 1–4 in the original's order);
  - the parameter table of the spec, and the six meters (input peak, GR drawn right to left, and why: the input rising and the compressor descending);
  - the distance (DDELAY's rule, the jump and its click, a setting not a cue, D in ms and samples in the footer);
  - the sample-rate reading (D a time with each rate's prime; integrator anchored at 96 kHz; compressor in seconds);
  - the memory (exact sizing against the spec's `1 << 15`, 1.06 MiB at 384 kHz);
  - against the original: no DC blockers (the listening decides), one output fader, POWER;
  - the specification block (the Faust of the processor header) and the verification (the relative errors measured in Tasks 4–6, the mutations, the CPU of Task 9);
  - out of scope (aligning DDELAY and ADDELAY on `seam_delays.h`; cues and MIDI in Reaper).

- [ ] **Step 2: `doc/study/sscdo2/delrm-plugin/README.md`** (scope README): each file (`gen-ref.sh`, `refdump.cpp`, `dsp/*.dsp`, `cpu.cpp`, `mutations.md`, `tests/delrm_signal.h`, `tests/ref_windows.h`), the libraries it added to `_common/` (`seam_delays.h`, `seam_filters.h`, `seam_compressors.h`, `metresToPrimeSamples` in `seam_primes.h`), how to run (`FAUSTLIBS=... ./gen-ref.sh`, the ctest line, the cpu compile line), how it fits (references → `tests/ref/*.h` → the tests), the measured results, including the line-memory figure written as `1.06 MiB` so that the report can cite it. Add to `doc/study/sscdo2/README.md`'s table, after the stunedrev plugin row:
  `| delRM | the C++ plugin | delrm-plugin/ | plugins/delrm, equal to sdt.delrm* within <measured> of the peak; seam_delays.h, seam_filters.h, seam_compressors.h | none |`

- [ ] **Step 3: Registry.** Append to the "Works — SSCDO#2" family in `doc/plugins.toml`, after STUNEDREV:

```toml
  [[family.plugin]]
  name = "DELRM"
  io = "4ch → 4ch"
  screenshot = "delrm.png"
  faust = "seam.tedesco.lib"
  description = """
delRM of Studio sul Corpo d'Ombra #2 (Cortegiani, Tedesco): four channels, each processing its own input. Channels 1 and 3 add the input to itself delayed; channels 2 and 4 multiply the delayed input, the input and its integral, into an 11:1 compressor. The delay is DDELAY's distance in metres moved to the next prime, one for the four channels. Input meters and the gain reduction of channels 2 and 4, drawn in opposite directions"""
```

Change the counts from 18 to 19 in `doc/scripts/test-doc.sh` (lines 21, 35, 38: the number and the message), "eighteen" to "nineteen" in `doc/scripts/render-readme.py:31` and `CLAUDE.md:90,92`.
Run: `make -C doc doc && make -C doc test`
Expected: README regenerated with the new row and gallery entry; every check ok except the screenshot copy, which needs `docs/img/delrm.png` (Step 6).

- [ ] **Step 4: Log.** Append to `logs/2026-10-02-sscdo2-delrm.md` under `## Work`: the tasks as done, the deviations found during implementation (for instance a change of operation order in Task 4 or 5 to match the generated Faust), the verification numbers, the mutations and any hole they found, the CPU. Close with `## Open`: the host check (Step 6), the dcblocker listening, the 6 deferred minors of stunedrev if still open, the choir's survey.

- [ ] **Step 5: Report.** In `doc/study/sscdo2/report/parte2-delrm.tex`:
  - card `delrm-volume`: field 8 also cites `\studio{delrm-plugin}`;
  - card `delrm-dinamica`: field 6 adds `nel plugin, i meter GR dei canali 2 e 4 mostrano la riduzione di guadagno, da destra verso sinistra; il loro scorrere opposto a quello degli ingressi rende leggibile la finestra mentre si regola l'ASP880`;
  - card `delrm-distanza`: field 5 adds `le linee del plugin sono dimensionate esattamente per 30 m: \misurau{1.06}{\mebi\byte}{delrm-plugin} a \qty{384}{\kilo\hertz}` (check that `siunitx` knows `\mebi\byte`; otherwise write `\misura{1.06}{delrm-plugin}~MiB`).
  Run: `make -C doc/study/sscdo2/report check`
  Expected: build ok, every `\misura` verified, `delrm-plugin` cited (check.py rule 3: every folder of the studies README must be cited). Commit the sources with the rebuilt PDF.

- [ ] **Step 6: Commit, then hand over to Giuseppe** for the host check, which no test replaces:

```bash
git add plugins/delrm/doc doc logs CLAUDE.md
git commit -m "docs(delrm): plugin README, study, registry (nineteen plugins), log, report"
```

Ask Giuseppe to: load delrm in Reaper at 96 kHz (4-channel track); play `doc/study/sscdo2/delrm-comb/renders/ccb_dry.wav` through the four channels with output at 1 and POWER on; compare by ear with `delrm-comb/renders/` and `delrm-rm/renders/`; move the distance by hand (the click, the comb moving); raise the input level and watch GR 2 and GR 4 run right to left while the inputs run left to right; the same session at 48 kHz (D changes prime, the sound does not); send the screenshot of the window for `docs/img/delrm.png`.
With the screenshot: `make -C doc doc && make -C doc test` (every check ok), `python3 tools/check-uidesc.py` (no WARN), commit.
Then use superpowers:finishing-a-development-branch, and ask before `make -C doc publish` and before any `git push`.
