# lessness, the hub's detector — Implementation Plan (plan 2 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `lessness`, the pass-through plugin that shows what passes on `libseamhub` (members, live states, proposals with each receiver's lifecycle, the sheet), and fold the `leaves`/`arrives` pairs a host produces when it toggles a plugin into one `resumes` line.

**Architecture:** The fold is a pure function over the hub's line queue, called by the writer thread before it writes, so a toggle never reaches the file, the recent lines or the detector as two lines; it changes no ABI.
`lessness` is a VST3 plugin in the shape of `strx` (pass-through, no parameters) whose views poll the hub from the GUI thread through `HubClient`; it never registers as a member, so it neither starts a session nor appears in one.
What the views draw is computed by a pure, SDK-free header, tested on its own.

**Tech Stack:** C++17, CMake (Xcode generator), doctest, VST3 SDK + VSTGUI, Python 3 (`tools/hub-sheet.py`).

**Spec:** `docs/superpowers/specs/2026-11-25-seam-hub-design.md` — *Design 3* (the detector), *Design 2* (the sheet), *Order of work* step 2.
Plan 1 (`docs/superpowers/plans/2026-11-25-seam-hub.md`) built the hub; its README (`plugins/_common/hub/README.md`) is the map, and its *Open points* name the two items this plan closes (the toggle pairs; the GUI saying that a lifecycle follows the receiver's current stone).

## Decisions taken with Giuseppe (2026-11-26)

- The detector's name is **lessness**: *seam-less-ness*, the suite going on; a plugin that shows every seam; and Beckett's *Lessness*, a text made of recombined sentences.
  Directory and target `lessness`, title and host name `SEAM LESSNESS`.
- A `leaves` followed within 1 s by an `arrives` of the same `name#instance` is written as **one line**, verb `resumes` (seven letters, so the verb column of 8 holds), object `after <ms> ms`, at the time of the arrival.

## Global Constraints

- `lessness` touches no sound: output equals input sample for sample, on any channel count with as many outputs as inputs.
- `lessness` has no automatable parameters and no state beyond the empty blob; it changes nothing but the sheet's folder and "start a new sheet" (spec, *Design 3*).
- It never applies, ignores or shows a proposal on anyone's behalf, and its view never counts as `shown`: it reads with `lifecycleOf`, never `transition`.
- It never registers on the hub; every hub call it makes is a GUI-thread call (`members`, `proposals`, `lifecycleOf`, `recentLines`, `sheetStatus`, `setSheetFolder`, `newSheet`); its audio thread never touches the hub.
- With no dylib (null mode) it loads, passes audio, and says `hub unavailable`.
- No ABI change: `SEAM_HUB_VERSION` stays 1, `seam_hub.h` is not edited.
- Window: `doc/style/ui-style.md`, started from `plugins/_template/resource/_template.uidesc`; all text `TextLight`; the alarm is a `MeterInv` mark beside white text, never red text.
- Comments say *why* and cite files; Markdown one sentence per line; code, commits and docs in English.
- Tests: `cmake -B build-test -DSEAM_BUILD_TESTS=ON -DSEAM_BUILD_PLUGINS=OFF`, run with `ctest --test-dir build-test -C Debug -R <name>`; plugin tree `build`.
- No plugin other than `lessness` is touched.

## Review Focus

1. **The host toggles the last member and the host thread must not wait the fold window**: `unregister` still returns as soon as the writer has taken the `leaves` (acknowledged as held), not after 1 s — test in Task 2.
2. **A short grace or a quit while a `leaves` is still young**: the writer writes it, unfolded, before closing the sheet; nothing is lost or carried into the next sheet — tests in Task 2.
3. **Two instances of one name** (`strx#1` leaves, `strx#2` arrives in the same millisecond): not folded, both lines kept — test in Task 1.
4. **A receiver whose stone changed after deciding**: its decision vanishes from `lifecycleOf`; the proposals pane must say why instead of looking empty — test in Task 3.
5. **`lessness` on a bus of 1, 2, 4 or 8 channels, in place or not**: samples identical; unequal in/out refused — test in Task 4.

---

## File Structure

```
plugins/_common/hub/
  seam_hub_fold.h         new, pure: queue Entry, fold(), kFoldMs
  seam_hub.cpp            the line queue holds Entry; drain() folds; recent fed after the fold
  seam_hub_sheet.h        grammar comment: verb `resumes`
  README.md               the fold, the recent lines after it; two Open points closed
tools/hub-sheet.py        VERBS gains `resumes`
tests/
  seam_hub_fold_test.cpp            new
  seam_hub_writer_test.cpp          toggle and recent-lines cases adjusted, fold cases added
  ref/hub-sheet-golden.txt          one `resumes` line
  lessness_view_test.cpp            new
plugins/lessness/
  CMakeLists.txt
  resource/lessness.uidesc
  source/lessness_ids.h  lessness_view.h  lessness_panes.h  lessness_buttons.h
         lessness_processor.h  lessness_processor.cpp  version.h
  doc/README.md
CMakeLists.txt            SEAM_PLUGINS gains lessness
doc/plugins.toml, doc/scripts/test-doc.sh, doc/style/ui-style.md, CLAUDE.md, README.md (generated)
```

---

### Task 1: The fold, pure

**Files:**
- Create: `plugins/_common/hub/seam_hub_fold.h`, `tests/seam_hub_fold_test.cpp`
- Modify: `plugins/_common/hub/seam_hub_sheet.h` (grammar comment: `resumes` among the verbs, with its object), `tools/hub-sheet.py:18` (`VERBS`), `tests/ref/hub-sheet-golden.txt` (one `resumes` line), `tests/CMakeLists.txt`

**Interfaces:**
- Produces, namespace `Seam::hubfold`:

```cpp
enum class Kind : uint8_t { Other, Arrives, Leaves };
struct Entry {
    std::string text;          // the event line and its rows, '\n'-separated (as today's queue strings)
    int64_t     unixMs = 0;    // the event's time
    Kind        kind = Kind::Other;
    std::string who;           // "strx#1" for Arrives and Leaves, empty otherwise
    std::string masks;         // the member's "says … hears …" text, for Arrives AND Leaves
    bool        remembered = false;   // already copied into the recent lines (Task 2)
};
constexpr int64_t kFoldMs = 1000;
// Folds, in place, every Leaves followed by an Arrives with the same `who`
// and the same `masks`, at most windowMs later: the Leaves is erased and the
// Arrives becomes one `resumes` line at the Arrives' time, object
// "after <dt> ms". Returns how many leading entries are ready to write: all
// of them, unless some Leaves still without a partner is younger than
// windowMs (nowMs - unixMs < windowMs), in which case the count stops just
// before the first such Leaves. windowMs = 0 folds what pairs are present
// and returns q.size().
size_t fold(std::deque<Entry>& q, int64_t nowMs, int64_t windowMs);
```

The `resumes` line is built with `Seam::hubsheet::event(arrivesMs, who, "resumes", "after " + std::to_string(dt) + " ms")`.
An Arrives with different masks is a real change of declaration: both lines stay.

- [ ] **Step 1: Write `tests/seam_hub_fold_test.cpp`** (helpers build Entries with `Seam::hubsheet::event`):
  - `"a toggle in the same millisecond is one resumes line"`: [Leaves strx#1 @1000, Arrives strx#1 @1000], now 1000, window 1000 → size 1, `text` contains `"strx#1"`, `"resumes"`, `"after 0 ms"`; returns 1.
  - `"lines between the pair stay in their place"`: [Leaves a#1 @0, Other @10, Arrives a#1 @20] → [Other, resumes]; returns 2.
  - `"a young leave waits, and what follows it waits too"`: [Other @0, Leaves a#1 @500, Other @600], now 900 → returns 1, size 3.
  - `"an old leave is ready"`: same queue, now 1600 → returns 3, nothing folded.
  - `"a return after the window is not folded"`: [Leaves a#1 @0, Arrives a#1 @1500], now 1600 → size 2, returns 2.
  - `"two instances are two members"`: [Leaves strx#1 @0, Arrives strx#2 @0] → size 2 (Review Focus 3).
  - `"a change of masks is kept"`: same who, different `masks` → size 2.
  - `"window zero writes everything"`: [Leaves a#1 @0], now 0, window 0 → returns 1.
  - `"chains fold pairwise"`: [L a#1 @0, A a#1 @0, L a#1 @5, A a#1 @5] → two `resumes` lines.
- [ ] **Step 2: Register `seam_hub_fold_test`** in `tests/CMakeLists.txt` beside `seam_hub_sheet_test` (same include dirs, no dylib); add `resumes` to the golden file and to `VERBS`.
- [ ] **Step 3: Run** — `cmake --build build-test --config Debug --target seam_hub_fold_test`: fails, header missing.
- [ ] **Step 4: Implement `seam_hub_fold.h`**, with a top comment saying why (Reaper's same-millisecond `setActive` toggle, the 2026-11-25 test in Reaper) and why the window holds the whole tail rather than the `leaves` alone (the sheet stays in order of arrival).
- [ ] **Step 5: Run** — `ctest --test-dir build-test -C Debug -R "seam_hub_fold|seam_hub_sheet|hub_sheet_golden"` passes.
- [ ] **Step 6: Commit** — `feat(hub): fold a host's leave-and-return into one resumes line (pure)`.

---

### Task 2: The writer folds

**Files:**
- Modify: `plugins/_common/hub/seam_hub.cpp` (queue type, `enqueue`, `registerLocked`, `unregisterLocked`, `Sheet::drain`, `Sheet::spill`, `writerMain`), `plugins/_common/hub/README.md`
- Test: `tests/seam_hub_writer_test.cpp`

**Interfaces:**
- Consumes: Task 1's `Seam::hubfold::Entry`, `fold`, `kFoldMs`.
- Produces: no new API. Behaviour:
  - `SeamHub::lines` becomes `std::deque<Seam::hubfold::Entry>`; `enqueue(Entry, bool mirrored = true)` keeps the drop-oldest bound and the test mirror (raw lines, unchanged: the state tests still see `arrives` and `leaves`).
  - `registerLocked` enqueues `Kind::Arrives` with `who` and `masks`; `unregisterLocked` enqueues `Kind::Leaves` with the same `masks` text, built from the slot's `says`/`hears` before the epoch closes.
  - `recent` (what `seam_hub_v1_recent_lines` returns) is fed **after the fold**: `rememberLocked` moves out of `enqueue` and runs on every ready entry not yet `remembered`, so the detector shows what the sheet carries.
  - `drain(int64_t windowMs)`: under `lineMutex`, first `fold(hub->lines, nowMs(), windowMs)` and remember the ready prefix, **whether or not a sheet is open** (held lines must still reach the recent lines); then swap out only the ready prefix into the batch.
    It still returns `enqSeq`: a young `leaves` kept in the queue is held, which is what the acknowledgement already means ("written, or held"), so the last `unregister` never waits for the window (Review Focus 1).
  - `writerMain` drains with `kFoldMs` on its ticks and with `0` on stop, on renew, and once more right before `sheet.close()`, so no young `leaves` outlives its sheet (Review Focus 2); `spill` writes `Entry::text`.

- [ ] **Step 1: Adjust the existing cases and add the new ones** in `tests/seam_hub_writer_test.cpp`:
  - `"a toggle within the grace continues the same sheet"`: expect `arrives` 1, `resumes` 1, `leaves` 1 (the toggle is now one line), with a comment saying why.
  - `"the recent lines carry rows and state lines"`: read the table rows with `eventually(...)`, since rows reach the recent lines on the writer's tick.
  - `"a host toggle is written as one resumes line"`: register `strx`, unregister and register again with the same instance at once, wait one tick past `kFoldMs`; the file has `arrives` 1, `resumes` 1 (object `after 0 ms` or a few ms), no `leaves`; `recentText()` has `resumes` and no `strx#1        leaves`.
  - `"the last unregister does not wait for the fold window"`: one member; `unregister` returns in under 500 ms (Review Focus 1).
  - `"a leave with nobody back is written after the window"`: after `kFoldMs` + 2 ticks the file ends with `leaves`.
  - `"a short grace writes a young leave before closing"`: grace 200 ms, register, unregister, `waitWriterGone()`; the one file ends with `leaves`, and a second session's file does not contain it.
  - `"quit with a lingering idle writer returns at once and the sheet ends with leaves"` passes unchanged (teardown drains with 0).
- [ ] **Step 2: Run** — `ctest --test-dir build-test -C Debug -R seam_hub_writer`: the new cases and the adjusted toggle case fail.
- [ ] **Step 3: Implement** in `seam_hub.cpp`, keeping the lock order comment true (fold and remember run under `lineMutex` only).
- [ ] **Step 4: Run the whole hub suite repeatedly** — `ctest --test-dir build-test -C Debug -R "seam_hub|hub_sheet|strx_hub" --repeat until-fail:5` passes.
- [ ] **Step 5: README** — in *The session sheet*, a paragraph *A toggle is one line* (why, the 1 s window, why it costs the host nothing, `resumes` in the verbs); in *recent lines*, "after the fold, on the writer's tick"; remove the toggle item from *Open points*.
- [ ] **Step 6: Commit** — `feat(hub): the writer folds a toggle into resumes; recent lines follow the sheet`.

---

### Task 3: What lessness shows, pure

**Files:**
- Create: `plugins/lessness/source/lessness_view.h`, `tests/lessness_view_test.cpp`
- Modify: `tests/CMakeLists.txt` (include `plugins/lessness/source` and `plugins/_common/hub`; no dylib, no SDK)

**Interfaces:**
- Consumes: `seam_hub.h` types; `Seam::hubsheet::who`, `stateObject`, `tableObject`, `sectionsObject`, `idRef` from `seam_hub_sheet.h`, so the window and the sheet use the same words.
- Produces, namespace `Seam::lessness`:

```cpp
using LifecycleOf = std::function<int32_t(uint32_t member, uint32_t epoch, uint64_t id)>;

std::string memberLine(const SeamHubMember& m);
//   "strx#1      stone 1  says -  hears pink glide decoder"   (stone "-" when 0 or no state)
std::string stateLine(const SeamHubMember& m);
//   hubsheet::stateObject(m.state) when hasState, else "no state"

struct Receiver { std::string who; int32_t lifecycle; };   // SeamHubLifecycle
struct ProposalRow {
    uint64_t id; bool pending;           // some receiver at Arrived or Shown
    std::string head;                    // "band-table #12  strx#1  stone 1  W  14:32  646 s"
    std::vector<Receiver> receivers;     // every member with lifecycle != None, in member order
};
std::vector<ProposalRow> proposalRows(const SeamHubProposal* p, int32_t n,
                                      const SeamHubMember* m, int32_t k, const LifecycleOf& of);
//   pending first, then the rest; newest first within each group
std::string receiversLine(const ProposalRow& r);
//   "bastone#1 shown  bastooge#1 applied", or "heard by nobody now"
extern const char* const kStoneNote;
//   "a receiver that changes stone no longer shows the decisions it took; the sheet keeps them"

std::string tildePath(const std::string& path, const char* home);   // $HOME prefix -> "~"
std::string sheetLine(const SeamHubSheetStatus& s, bool available);
//   "hub unavailable" | "no session" (empty path) |
//   "~/Documents/SEAM/sheets/2026-…-seam-sheet.txt  writing" |
//   "… folder unreachable  120 lines held  3 dropped"
bool sheetAlarm(const SeamHubSheetStatus& s, bool available);
//   true when !available, or a path exists and writing == 0
```

The head of a band table names the decoder mode it was measured under (`testWord` of `decoder.test` when `hasDecoder`, else `-`), the post time as local `HH:MM`, and `integrationSec` rounded; a sections head names the block and the section count.

- [ ] **Step 1: Write `tests/lessness_view_test.cpp`**:
  - `"a member line names who, stone and masks"`; `"no state reads as no state"`.
  - `"a pending proposal comes first"`: ids 1 (applied by its only receiver) and 2 (arrived) → rows `[2, 1]`, `rows[0].pending`.
  - `"each receiver keeps its own lifecycle"`: two members, `of` returns Shown for one and Applied for the other → `receiversLine` has both words.
  - `"a proposal nobody hears says so"`: `of` returns None for all → `receiversLine == "heard by nobody now"`, not pending (Review Focus 4: the pane shows this line, with `kStoneNote` under the pane, never an empty row).
  - `"the head carries mode, time and integration"`: a table with `hasDecoder`, `test = 1`, `integrationSec = 646.6` → head contains `" W "` and `"647 s"`.
  - `"the sheet line and its alarm"`: unavailable → `"hub unavailable"`, alarm; empty path → `"no session"`, no alarm; path writing → ends `"writing"`, no alarm; `writing 0, pendingLines 120, droppedLines 3` → contains `"120 lines held"` and `"3 dropped"`, alarm.
  - `"the home folder reads as ~"`: `tildePath("/Users/g/Documents/x", "/Users/g") == "~/Documents/x"`; a path outside home unchanged; null home unchanged.
- [ ] **Step 2: Run** — fails, header missing.
- [ ] **Step 3: Implement `lessness_view.h`.**
- [ ] **Step 4: Run** — `ctest --test-dir build-test -C Debug -R lessness_view` passes.
- [ ] **Step 5: Commit** — `feat(lessness): what the detector shows, pure and tested`.

---

### Task 4: The plugin

**Files:**
- Create: `plugins/lessness/CMakeLists.txt` (copy of `plugins/strx/CMakeLists.txt`, names changed, bundle `io.github.s-e-a-m.lessness`), `source/lessness_ids.h`, `source/version.h`, `source/lessness_processor.{h,cpp}`, `source/lessness_panes.h`, `source/lessness_buttons.h`, `resource/lessness.uidesc` (from `_template.uidesc`)
- Modify: root `CMakeLists.txt` (`SEAM_PLUGINS` gains `lessness`), `tests/CMakeLists.txt`
- Test: `tests/lessness_passthrough_test.cpp`

**Interfaces:**
- Consumes: Task 3's functions; `HubClient` (`plugins/_common/hub/seam_hub_client.h`).
- Produces:
  - FUID `LessnessProcessorUID(0x5E4D0017, 0xA1B2C3D4, 0x4C455353, 0x00000017)` (23rd plugin, "LESS"); category `"Fx|Analyzer"`; host name `SEAM LESSNESS`; subtitle `Hub Detector`; tagline `states · proposals · sheet · no sound touched`.
  - `void Seam::lessness::passThrough(float* const* in, float* const* out, int32_t channels, int32_t frames)` and the `double` overload, in `lessness_processor.h` as inline free functions: copy unless `in[c] == out[c]`.
  - Buses: one input, one output, default stereo; `setBusArrangements` accepts any arrangement with equal channel counts on both sides (1 to 64), refuses otherwise.
  - Custom views (names in `lessness_ids.h`), each on its own `CVSTGUITimer` at 250 ms (the writer's tick: nothing on the hub changes faster in the sheet), all snapshots taken in the view's timer from `HubClient::instance()`:
    - `LessnessMembers`: one `memberLine` and, indented, one `stateLine` per member.
    - `LessnessProposals`: `proposalRows` from `proposals(0, …)` of the whole store (`SEAM_HUB_STORE`), each as its `head` then `receiversLine`; `kStoneNote` on the pane's last line.
    - `LessnessLines`: the newest `recentLines()` that fit the pane, bottom-aligned, clipped not wrapped.
    - `LessnessSheet` (FOOTER): `sheetLine` in white, with a 10 × 10 `MeterInv` square before it when `sheetAlarm`.
    - `LessnessFolderButton` (`FOLDER`): a `CNewFileSelector` in `kSelectDirectory` mode, run asynchronously; on a choice, `setSheetFolder`; the sheet line then reads `next sheet in <tildePath(folder)> · NEW SHEET to start it` until the current path is inside that folder.
    - `LessnessNewSheetButton` (`NEW SHEET`): `newSheet()`.
    Both buttons are GUI actions on a view with no bound parameter, as `strx`'s SAVE (`strx_savebutton.h`).
  - Layout: L format (as `strx`, for the width its text panes need, having no controls), frame `640, 620`: HEADER; OPS with FOLDER and NEW SHEET; three panes under `— MEMBERS —`, `— PROPOSALS —`, `— SHEET —`; FOOTER with `LessnessSheet` and the logo. Text in `SourceCodePro-Light` 11, so sheet lines keep their columns.
  - No hub registration anywhere in the plugin, and no hub call from `process()`.

- [ ] **Step 1: Write `tests/lessness_passthrough_test.cpp`**: `"output equals input on 1, 2, 4 and 8 channels"` (random floats, `CHECK(out == in)` bitwise), `"in place is a no-op"` (`out == in` pointers, buffer unchanged), the `double` overload once (Review Focus 5).
- [ ] **Step 2: Run** — fails.
- [ ] **Step 3: Copy the template, write the `.uidesc`, run the lint** — `python3 tools/check-uidesc.py plugins/lessness/resource/lessness.uidesc` passes before any C++.
- [ ] **Step 4: Implement the processor, views and buttons**; register the plugin in `SEAM_PLUGINS`.
- [ ] **Step 5: Run** — `ctest --test-dir build-test -C Debug -R "lessness"` passes; `cmake --build build --config Release --target lessness` builds and the validator passes; `ctest --test-dir build -C Release -R "minos|uidesc"` passes.
- [ ] **Step 6: Commit** — `feat(lessness): the hub's detector, a pass-through that shows states, proposals and the sheet`.

---

### Task 5: Documentation, registry, the check in the host

**Files:**
- Create: `plugins/lessness/doc/README.md`
- Modify: `doc/plugins.toml` (`LESSNESS` in *Measurement and processing*, io `any → same`, faust empty, screenshot `lessness.png`), `doc/scripts/test-doc.sh` (22 → 23 in both checks), the prose counts in `doc/scripts/publish.py` and `CLAUDE.md` ("twenty-two" → "twenty-three"), `doc/style/ui-style.md` (FOLDER and NEW SHEET beside SAVE, same reasoning), `plugins/_common/hub/README.md` (the detector is `lessness`; the stone-note Open point closed), `README.md` (regenerated by `make -C doc doc`), `TODO.md`

- [ ] **Step 1: Write `plugins/lessness/doc/README.md`**: what it shows and why it acts on nothing (the spec's *The operator sees everything first*), why it does not register (it would open sessions and appear in them), the fold seen from the operator's side, the name.
- [ ] **Step 2: Registry and counts** — `make -C doc doc`; `make -C doc test` passes with 23.
- [ ] **Step 3: Full tests** — `cmake --build build-test --config Debug && ctest --test-dir build-test -C Debug` all pass.
- [ ] **Step 4: Commit** — `docs(lessness): registry, README, vocabulary; 23 plugins`.
- [ ] **Step 5: In the host (Giuseppe, Reaper)**: `lessness` on the master, `multipink` → STONE → `strx` as in a calibration session.
  The members pane lists `multipink#1` and `strx#1` with their states; stopping and starting the transport writes `resumes`, not `leaves`/`arrives`, in the sheet and in the lines pane; FOLDER moves the next sheet, NEW SHEET starts it; with the folder on an unmounted volume the footer shows the red mark and the held count; `tools/hub-sheet.py --check` passes on the sheet.
  A screenshot `docs/screenshots/lessness.png` for the registry.
