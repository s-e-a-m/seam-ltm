# strx Cross-Loop Accumulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When strx observes ltglide in LOOP, the spectrum accumulates a per-bin MIN over completed passes instead of visually resetting every cycle.

**Architecture:** A pure three-outcome boundary decision (`holdAction`) in the digest replaces the boolean `shouldResetHold` inside `CalbusWatch`, which drives the audio thread through two epoch atomics (`sessionEpoch` new, `holdEpoch` existing). The `Analyzer` gains a MIN accumulator folded on each completed pass and published through the existing SPSC triple-buffer; the spectrum view draws the accumulation as the primary curve, which survives the loop stopping.

**Tech Stack:** C++17, VST3 SDK / VSTGUI (view + processor), doctest unit tests, CMake/Xcode.

**Spec:** `docs/superpowers/specs/2026-07-21-strx-crossloop-accumulation-design.md`

## Global Constraints

- Repo: `/Users/giuseppe/Documents/github/seam/librerie/seam-ltm`, branch `calbus`.
- DSP stays SDK-free in `strx_dsp.h` / `strx_calbus_digest.h` (unit-testable without a host).
- The audio thread never touches the calibration bus; GUI→DSP crossings are relaxed atomics read once per block.
- Accumulator semantics (spec): per-bin MIN in dB over COMPLETED passes only; a partial pass never folds; one continuous LOOP run = one session; the accumulation clears at the START of the next session, not when the loop stops.
- Status line already shows the pass counter (`strx_status.h:86-88`, "pass %llu") — the spec's status-line requirement is satisfied by existing code; do not touch `strx_status.h`.
- Every new test must be verified by mutation (break the code, confirm RED, revert) per feedback_verify_tests_by_mutation.
- Test tree: `build-test` (Xcode, plugins OFF). Plugin tree: `build` (Xcode, owns the VST3 symlinks).
- Commit messages end with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01Wef47rchiCfvQ8Qac4x7YZ`

---

### Task 1: Three-outcome boundary decision in the digest

**Files:**
- Modify: `plugins/strx/source/strx_calbus_digest.h` (the `shouldResetHold` block, lines 48-76)
- Test: `tests/strx_calbus_digest_test.cpp`

**Interfaces:**
- Consumes: `CalbusDigest` and `digest()` (existing, unchanged).
- Produces: `enum class HoldAction { None, SessionStart, PassBoundary };` and `HoldAction holdAction(const CalbusDigest& d, uint64_t& lastPass)` in `namespace Seam::strx`. `shouldResetHold(d, lastPass)` remains, reimplemented as `holdAction(...) != HoldAction::None` (keeps every existing test meaningful as regression coverage). Task 3 calls `holdAction` from the watch.

- [ ] **Step 1: Write the failing tests**

Append to `tests/strx_calbus_digest_test.cpp` (after the existing `shouldResetHold` cases). Add `using Seam::strx::holdAction;` and `using Seam::strx::HoldAction;` next to the existing `using` lines at the top of the file.

```cpp
//──────────────────────────────────────────────────────────────────────────
// holdAction — the three-outcome refinement of shouldResetHold that the
// cross-loop accumulator needs: a SessionStart clears the accumulation, a
// PassBoundary folds the completed pass into it. Same seam as above:
// exercised through digest() + real record builders.
//──────────────────────────────────────────────────────────────────────────

TEST_CASE("holdAction: the first pass after idle is a session start") {
    uint64_t lastPass = 0;
    SeamCalbusRecord recs[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(recs, 1, true), lastPass) == HoldAction::SessionStart);
    CHECK(lastPass == 1u);
    CHECK(holdAction(digest(recs, 1, true), lastPass) == HoldAction::None);  // same pass re-polled
}

TEST_CASE("holdAction: loop passes after the first are pass boundaries") {
    uint64_t lastPass = 0;
    SeamCalbusRecord pass1[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(pass1, 1, true), lastPass) == HoldAction::SessionStart);
    SeamCalbusRecord pass2[1] = { glide(1, true, 2) };   // loop advanced, still active
    CHECK(holdAction(digest(pass2, 1, true), lastPass) == HoldAction::PassBoundary);
    CHECK(holdAction(digest(pass2, 1, true), lastPass) == HoldAction::None);
    SeamCalbusRecord pass3[1] = { glide(1, true, 3) };
    CHECK(holdAction(digest(pass3, 1, true), lastPass) == HoldAction::PassBoundary);
}

TEST_CASE("holdAction: a different STONE after a gap starts a NEW session, never a boundary") {
    // Same scenario as the cross-instance collision above: fold STONE 2's
    // first pass into STONE 1's accumulation and the whole measurement lies.
    uint64_t lastPass = 0;
    SeamCalbusRecord stoneOne[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(stoneOne, 1, true), lastPass) == HoldAction::SessionStart);
    SeamCalbusRecord gap[1] = { glide(1, false, 1) };     // nothing sounding
    CHECK(holdAction(digest(gap, 1, true), lastPass) == HoldAction::None);
    CHECK(lastPass == 0u);                                // sentinel cleared
    SeamCalbusRecord stoneTwo[1] = { glide(2, true, 1) }; // different instance, its own pass 1
    CHECK(holdAction(digest(stoneTwo, 1, true), lastPass) == HoldAction::SessionStart);
}

TEST_CASE("holdAction: pink or idle emitters never act and clear the sentinel") {
    uint64_t lastPass = 0;
    SeamCalbusRecord activePass[1] = { glide(1, true, 5) };
    CHECK(holdAction(digest(activePass, 1, true), lastPass) == HoldAction::SessionStart);
    CHECK(lastPass == 5u);
    SeamCalbusRecord pinkRec[1] = { pink(1, true, 0) };
    CHECK(holdAction(digest(pinkRec, 1, true), lastPass) == HoldAction::None);
    CHECK(lastPass == 0u);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build-test --config Debug --target strx_calbus_digest_test 2>&1 | tail -3
```
Expected: compile error — `holdAction` and `HoldAction` not declared.

- [ ] **Step 3: Implement holdAction, delegate shouldResetHold**

In `plugins/strx/source/strx_calbus_digest.h`, replace the whole `shouldResetHold` function (its doc comment stays, lines 48-65, with the small edits shown) with:

```cpp
// Classifies what a NEW bus reading means for the measurement:
//   SessionStart — first pass after a no-glide state: a new measurement
//                  session begins, the previous accumulation must go.
//   PassBoundary — passCounter advanced within a session: the previous pass
//                  completed and can fold into the accumulation.
//   None         — nothing changed (same pass re-polled, idle, or pink).
// `lastPass` is in/out state owned by the caller (one instance per watch).
//
// Keying the decision on `passCounter` ALONE is the bug the sentinel fixes:
// `passCounter` is per-emitter-instance and starts at 1 on that instance's
// first pass (GlideTransport::beginPass() increments before a pass sounds —
// see ltglide_dsp.h), so STONE 1's pass 1 and STONE 2's pass 1 are the same
// number. Resetting `lastPass` to 0 whenever no glide is sounding closes that
// gap: 0 can never collide with a real pass number (an ACTIVE glide always
// has passCounter >= 1), and there is always a moment with no glide active
// between two different STONEs being measured one at a time. That same
// sentinel is what distinguishes SessionStart (lastPass == 0: we came from a
// no-glide state) from PassBoundary (a loop advancing within one session,
// where `active` never drops and the sentinel never clears).
enum class HoldAction { None, SessionStart, PassBoundary };

inline HoldAction holdAction(const CalbusDigest& d, uint64_t& lastPass) {
    if (!d.glide) {
        lastPass = 0;
        return HoldAction::None;
    }
    if (d.passCounter != lastPass) {
        const bool fresh = (lastPass == 0);
        lastPass = d.passCounter;
        return fresh ? HoldAction::SessionStart : HoldAction::PassBoundary;
    }
    return HoldAction::None;
}

// Boolean view of holdAction: does the hold need clearing at all? Kept so the
// pre-accumulation tests keep pinning the shared sentinel semantics.
inline bool shouldResetHold(const CalbusDigest& d, uint64_t& lastPass) {
    return holdAction(d, lastPass) != HoldAction::None;
}
```

- [ ] **Step 4: Run the digest tests, all green (old + new)**

```bash
cmake --build build-test --config Debug --target strx_calbus_digest_test 2>&1 | tail -3 && ctest --test-dir build-test -C Debug -R strx_calbus_digest_test --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 5: Verify by mutation**

In `holdAction`, swap the ternary to `fresh ? HoldAction::PassBoundary : HoldAction::SessionStart`. Rebuild + rerun: the new tests MUST fail. Revert the mutation, rebuild + rerun: green. Then mutate `lastPass = 0;` in the `!d.glide` branch to `lastPass = lastPass;`: the cross-STONE test MUST fail. Revert, rebuild, green.

- [ ] **Step 6: Commit**

```bash
git add plugins/strx/source/strx_calbus_digest.h tests/strx_calbus_digest_test.cpp
git commit -m "feat(strx): three-outcome holdAction (session start vs pass boundary)"
```
(Use the heredoc form with the Global Constraints trailer lines.)

---

### Task 2: MIN accumulator in the Analyzer

**Files:**
- Modify: `plugins/strx/source/strx_dsp.h` (`AnalysisFrame` lines 19-41, `Analyzer` — `reset()` line 58, `resetHold()` line 82, `analyze()` line 108, private members line 169+)
- Test: `tests/strx_dsp_test.cpp`

**Interfaces:**
- Consumes: `seam::fft::Welch::holdDb()/numBins()/resetHold()` (existing).
- Produces, on `Seam::strx::Analyzer`: `void completePass()` (fold hold into acc by per-bin min, `++accPasses_`, then clear hold), `void startSession()` (clear acc and hold, `accPasses_ = 0`), `int accPasses() const`. On `AnalysisFrame`: `float accM[kNumBins]`, `float accS[kNumBins]`, `int accPasses`. Task 3's processor calls `completePass()`/`startSession()`; Task 4's view reads `frame.accM/accS/accPasses`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/strx_dsp_test.cpp`:

```cpp
TEST_CASE("cross-pass MIN keeps what every pass contains, discards what one pass had") {
    Analyzer a; a.prepare(48000.0);
    const int N = a.fftSize();
    std::vector<float> tone(2*N), mixed(2*N), quiet(2*N, 0.0f);
    for (int i = 0; i < 2*N; ++i) {
        tone[i]  = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
        mixed[i] = tone[i] + 0.5f*std::sin(2.0*M_PI*3000.0*i/48000.0);
    }
    // Per pass: feed the pass signal, then two full quiet windows to flush
    // the Welch ring (resetHold leaves the ring untouched — see the existing
    // "Analyzer resetHold clears the published hold" case; silence never
    // raises a max-hold, so the pass curve is unharmed), then fold.
    auto feedPass = [&](std::vector<float>& sig) {
        a.analyze(sig.data(), sig.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.completePass();
    };
    feedPass(mixed);   // pass 1: 1 kHz + an intermittent 3 kHz disturbance
    feedPass(tone);    // pass 2: clean 1 kHz — the disturbance is absent
    a.analyze(quiet.data(), quiet.data(), 2*N);   // publish acc into the frame

    const auto& f = a.frame();
    CHECK(f.accPasses == 2);
    const int bin1k = int(std::lround(1000.0 * N / 48000.0));
    const int bin3k = int(std::lround(3000.0 * N / 48000.0));
    // Present in EVERY pass -> survives the MIN. Bounded strictly below 0:
    // AnalysisFrame zero-initializes accM, so a never-copied 0.0f must fail.
    CHECK(f.accM[bin1k] > -40.0f);
    CHECK(f.accM[bin1k] < 0.0f);
    // Present in ONE pass only -> discarded by the MIN.
    CHECK(f.accM[bin3k] < -60.0f);
    // Same L=R signal -> no side energy; also proves accS is copied (its
    // never-copied default 0.0f would fail this bound).
    CHECK(f.accS[bin1k] < -60.0f);
}

TEST_CASE("startSession clears the accumulator; acc changes only on completePass") {
    Analyzer a; a.prepare(48000.0);
    const int N = a.fftSize();
    std::vector<float> tone(2*N), quiet(2*N, 0.0f);
    for (int i = 0; i < 2*N; ++i) tone[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    const int bin1k = int(std::lround(1000.0 * N / 48000.0));

    a.analyze(tone.data(), tone.data(), 2*N);
    a.analyze(quiet.data(), quiet.data(), 2*N);
    a.completePass();
    a.analyze(quiet.data(), quiet.data(), 2*N);   // publish
    CHECK(a.frame().accPasses == 1);
    CHECK(a.frame().accM[bin1k] > -40.0f);

    // More audio WITHOUT a boundary must not touch the accumulation — this
    // is the partial-pass guarantee: no completePass(), no fold.
    a.analyze(tone.data(), tone.data(), 2*N);
    CHECK(a.frame().accPasses == 1);

    a.startSession();
    a.analyze(quiet.data(), quiet.data(), 2*N);   // publish
    CHECK(a.frame().accPasses == 0);
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build-test --config Debug --target strx_dsp_test 2>&1 | tail -3
```
Expected: compile error — `completePass` not a member of `Analyzer`.

- [ ] **Step 3: Implement the accumulator**

In `plugins/strx/source/strx_dsp.h`:

(a) `AnalysisFrame` — after the `holdM/holdS` block (line 39), before `numBins`:

```cpp
    // Cross-loop accumulation: per-bin MIN over completed passes' hold
    // curves (dB). Meaningful only when accPasses > 0. The MIN keeps what
    // EVERY pass contains — the system response — and discards intermittent
    // disturbances, which are absent from at least one pass.
    float accM[kNumBins] = {};  // dB
    float accS[kNumBins] = {};  // dB
    int accPasses = 0;          // completed passes folded so far
```

(b) `Analyzer` — after `resetHold()` (line 82):

```cpp
    // A completed pass: fold its hold curve into the cross-pass MIN
    // accumulator, then clear the hold for the next pass. The first fold
    // COPIES (a MIN against the -120 init floor would pin the accumulation
    // there forever). dB min == linear min (monotone map), so fold in dB.
    void completePass() {
        const float* hM = welchM_.holdDb();
        const float* hS = welchS_.holdDb();
        const int n = welchM_.numBins();
        for (int k = 0; k < n; ++k) {
            accM_[k] = (accPasses_ == 0) ? hM[k] : std::min(accM_[k], hM[k]);
            accS_[k] = (accPasses_ == 0) ? hS[k] : std::min(accS_[k], hS[k]);
        }
        ++accPasses_;
        resetHold();
    }
    // A new measurement session: previous accumulation and hold both go.
    // Partial passes never fold BY CONSTRUCTION: the only fold site is
    // completePass(), which the processor calls on a pass boundary — a
    // mid-pass stop leaves the partial hold to die here or in resetHold().
    void startSession() {
        std::fill(accM_, accM_ + AnalysisFrame::kNumBins, -120.0f);
        std::fill(accS_, accS_ + AnalysisFrame::kNumBins, -120.0f);
        accPasses_ = 0;
        resetHold();
    }
    int accPasses() const { return accPasses_; }
```

(c) `reset()` (line 58) — add before the slots loop:

```cpp
        std::fill(accM_, accM_ + AnalysisFrame::kNumBins, -120.0f);
        std::fill(accS_, accS_ + AnalysisFrame::kNumBins, -120.0f);
        accPasses_ = 0;
```

(d) `analyze()` — extend the per-bin copy loop (lines 131-134):

```cpp
        for (int k = 0; k < fr.numBins; ++k) {
            fr.specM[k] = mM[k]; fr.specS[k] = mS[k];
            fr.holdM[k] = hM[k]; fr.holdS[k] = hS[k];
            fr.accM[k]  = accM_[k]; fr.accS[k] = accS_[k];
        }
        fr.accPasses = accPasses_;
```

(e) private members — after `welchM_, welchS_` (line 177):

```cpp
    float accM_[AnalysisFrame::kNumBins] = {};
    float accS_[AnalysisFrame::kNumBins] = {};
    int   accPasses_ = 0;
```

- [ ] **Step 4: Run the strx DSP tests, all green**

```bash
cmake --build build-test --config Debug --target strx_dsp_test 2>&1 | tail -3 && ctest --test-dir build-test -C Debug -R strx_dsp_test --output-on-failure
```
Expected: `100% tests passed`.

- [ ] **Step 5: Verify by mutation**

Mutate `completePass()` to always `std::min` (drop the `accPasses_ == 0` copy): the first test MUST fail (`accM[bin1k]` pinned at -120). Revert, green. Mutate `analyze()` to skip the `fr.accM/accS` copy: both tests MUST fail on the frame checks (0.0f defaults). Revert, green.

- [ ] **Step 6: Commit**

```bash
git add plugins/strx/source/strx_dsp.h tests/strx_dsp_test.cpp
git commit -m "feat(strx): cross-pass MIN accumulator in the Analyzer"
```
(Trailer lines per Global Constraints.)

---

### Task 3: Wire the session epoch through watch and processor

**Files:**
- Modify: `plugins/strx/source/strx_calbus_watch.h` (constructor lines 31-32, poll lines 48-57, members 63-65)
- Modify: `plugins/strx/source/strx_processor.h` (atomics block lines 94-99)
- Modify: `plugins/strx/source/strx_processor.cpp` (`process()` epoch block lines 59-63)

**Interfaces:**
- Consumes: `holdAction`/`HoldAction` (Task 1), `Analyzer::completePass()/startSession()` (Task 2).
- Produces: `CalbusWatch(std::atomic<bool>& glideOut, std::atomic<uint32_t>& holdEpochOut, std::atomic<uint32_t>& sessionEpochOut)` — three-atomic constructor; the processor's `process()` reacts to both epochs. Task 4 needs nothing from here (it reads the digest and the frame).

- [ ] **Step 1: Extend the watch**

In `strx_calbus_watch.h`, constructor + members:

```cpp
    // `glideOut`, `holdEpochOut` and `sessionEpochOut` are the processor's
    // atomics, read by the audio thread once per process() block.
    CalbusWatch(std::atomic<bool>& glideOut, std::atomic<uint32_t>& holdEpochOut,
                std::atomic<uint32_t>& sessionEpochOut)
        : glide_(glideOut), holdEpoch_(holdEpochOut), sessionEpoch_(sessionEpochOut) {}
```

members (after `holdEpoch_`):

```cpp
    std::atomic<uint32_t>& sessionEpoch_;
```

Replace the `shouldResetHold` block in `poll()` (lines 55-57) with:

```cpp
        switch (holdAction(digest_, lastPass_)) {
        case HoldAction::SessionStart:
            // The processor's session branch clears BOTH accumulation and
            // hold, so a session start bumps only this epoch: bumping
            // holdEpoch too would race a fold against the just-cleared hold.
            sessionEpoch_.fetch_add(1, std::memory_order_relaxed);
            break;
        case HoldAction::PassBoundary:
            holdEpoch_.fetch_add(1, std::memory_order_relaxed);
            break;
        case HoldAction::None:
            break;
        }
```

Also update the comment above (lines 48-53): it names `shouldResetHold()`; reword to `holdAction()` ("the actual decision — session start vs pass boundary, with the per-emitter sentinel — is holdAction(), next to digest(), pure and unit-tested").

- [ ] **Step 2: Extend the processor**

In `strx_processor.h`, replace lines 96-99 with (declaration order matters — the watch's member-initializer references all three atomics):

```cpp
    std::atomic<bool>     specGlide_{false};
    std::atomic<uint32_t> holdEpoch_{0};
    std::atomic<uint32_t> sessionEpoch_{0};
    uint32_t              lastHoldEpoch_ = 0;
    uint32_t              lastSessionEpoch_ = 0;
    Seam::strx::CalbusWatch calbusWatch_{specGlide_, holdEpoch_, sessionEpoch_};
```

In `strx_processor.cpp` `process()`, replace lines 62-63 with:

```cpp
    const uint32_t se = sessionEpoch_.load(std::memory_order_relaxed);
    const uint32_t he = holdEpoch_.load(std::memory_order_relaxed);
    if (se != lastSessionEpoch_) {
        // New measurement session: previous accumulation and hold both go.
        // Any pending pass bump belonged to the old session — swallow it.
        lastSessionEpoch_ = se;
        lastHoldEpoch_    = he;
        analyzer_.startSession();
    } else if (he != lastHoldEpoch_) {
        lastHoldEpoch_ = he;
        analyzer_.completePass();   // fold the completed pass, then clear the hold
    }
```

- [ ] **Step 3: Full test suite + plugin build**

```bash
ctest --test-dir build-test -C Debug --output-on-failure 2>&1 | tail -5
cmake --build build --config Release --target strx 2>&1 | tail -5
```
Expected: all tests pass; strx.vst3 builds with no warnings about the new code. (The watch/processor wiring has no SDK-free unit harness — it is exercised in-host in Task 5.)

- [ ] **Step 4: Commit**

```bash
git add plugins/strx/source/strx_calbus_watch.h plugins/strx/source/strx_processor.h plugins/strx/source/strx_processor.cpp
git commit -m "feat(strx): session epoch drives startSession/completePass on the audio thread"
```
(Trailer lines per Global Constraints.)

---

### Task 4: Spectrum view — the measurement never resets per cycle

**Files:**
- Modify: `plugins/strx/source/strx_spectrum.h` (timer lambda lines 57-61, curve block lines 156-169, members line 198)

**Interfaces:**
- Consumes: `frame.accM/accS/accPasses` (Task 2), `CalbusDigest.firstActive/glide` (existing).
- Produces: display behavior only; nothing downstream.

- [ ] **Step 1: Track the non-glide-emitter flag**

Timer lambda becomes:

```cpp
            [this](VSTGUI::CVSTGUITimer*) {
                if (processor_) {
                    const auto& d = processor_->calbusWatch().poll();
                    glide_ = d.glide;
                    otherActive_ = (d.firstActive >= 0) && !d.glide;
                }
                invalid();
            }, kTimerMs, /*doStart*/true);
```

Members (next to `glide_`, keeping that member's comment which covers both):

```cpp
    bool glide_ = false;
    bool otherActive_ = false;   // a non-glide emitter (pink) is sounding
```

- [ ] **Step 2: Replace the curve-drawing block**

Replace the `if (glide_) { ... } else { ... }` block (lines 161-169) and the comment above it (lines 156-160) with:

```cpp
        // The visible measurement never resets per loop cycle (that per-cycle
        // reset was the reported malfunction). Pass 1 shows the building
        // hold; from the first fold on, the cross-pass MIN accumulation IS
        // the measurement — and it survives the loop stopping, until a pink
        // (non-glide) emitter takes over the observation or a new glide
        // session replaces it. Pink noise is stationary, so outside a
        // measurement one averaged curve per channel still says everything.
        const bool haveAcc  = frame.accPasses > 0;
        const bool measured = glide_ || (haveAcc && !otherActive_);
        if (measured) {
            drawCurve(haveAcc ? frame.accM : frame.holdM, frame.numBins, colorM_, /*alpha*/255);
            drawCurve(haveAcc ? frame.accS : frame.holdS, frame.numBins, colorS_, /*alpha*/255);
            drawCurve(frame.specM, frame.numBins, colorM_, /*alpha*/90);
            drawCurve(frame.specS, frame.numBins, colorS_, /*alpha*/90);
        } else {
            drawCurve(frame.specM, frame.numBins, colorM_, /*alpha*/255);
            drawCurve(frame.specS, frame.numBins, colorS_, /*alpha*/255);
        }
```

- [ ] **Step 3: Build the plugin**

```bash
cmake --build build --config Release --target strx 2>&1 | tail -3
```
Expected: strx.vst3 builds clean.

- [ ] **Step 4: Commit**

```bash
git add plugins/strx/source/strx_spectrum.h
git commit -m "feat(strx): spectrum draws the cross-pass accumulation, persistent across cycles"
```
(Trailer lines per Global Constraints.)

---

### Task 5: Full verification + host checklist for GS

**Files:** none (verification only).

- [ ] **Step 1: Entire test suite green**

```bash
ctest --test-dir build-test -C Debug --output-on-failure 2>&1 | tail -5
```
Expected: `100% tests passed` across all suite targets (regression: ltglide/calbus/fft tests untouched by this work must stay green).

- [ ] **Step 2: Rebuild strx for the DAW tree**

```bash
cmake --build build --config Release --target strx 2>&1 | tail -3
```
Expected: strx.vst3 rebuilt; the `build` tree owns the `~/Library/Audio/Plug-Ins/VST3` symlink (reference_vst3_symlink_ownership), so Reaper picks it up on rescan.

- [ ] **Step 3: Report the host checklist to GS (manual, in Reaper)**

1. ltglide in LOOP → strx spectrum: pass 1 builds the hold; from pass 2 the curve refines and NEVER resets at the cycle boundary; status line advances "pass N".
2. Stop the loop mid-pass → curve stays (partial pass discarded, accumulation intact).
3. Restart LOOP → the old curve is replaced by a fresh session.
4. Stop the loop, un-mute a multipink → spectrum returns to the live pink view; mute it again → the accumulated measurement reappears.
5. Two STONEs one after the other → the second's session never inherits the first's curve.
