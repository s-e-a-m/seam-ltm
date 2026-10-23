# Pd externals for the STONE chain — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `m2xhgr~`, `lr2xhgr~` and `bamodulex~` as Pd externals, sharing the VST3 plugins' DSP headers, with rehearsal help patches and a STONE-wired concert copy of Di Scipio's *UPU* timpani patch.

**Architecture:** First the plugins' inline DSP moves into `m2xhgr_dsp.h` / `bamodulex_dsp.h` (VST3 bit-identical). Each external is then split in two: a Pd-independent **engine** header in `pd/` (parameters, ramps, clipping, block processing, the paste-ready object line) tested with doctest under ctest, and a thin **glue** `.cpp` that only talks to `m_pd.h`. A standalone `pd/Makefile` builds fat `.d_fat` binaries against the installed Pd.

**Tech Stack:** C++17, Pd 0.56-2 API (`m_pd.h`), doctest + CMake/ctest (existing suite), make, Pd patch files.

**Spec:** `docs/superpowers/specs/2026-10-23-pd-stone-externals-design.md`

## Global Constraints

- Sample rate **48 kHz**; at any other rate the externals post a console warning and correct nothing.
- Pd **0.56-2** at `/Applications/Pd-0.56-2.app`, vanilla 32-bit float; headers at `/Applications/Pd-0.56-2.app/Contents/Resources/src` (override `PDINCLUDEDIR=`).
- Binaries: fat x86_64 + arm64, `.d_fat`, `-mmacosx-version-min=11.0`; no VST3 SDK, no downloads.
- Install dir: `~/Documents/Pd/externals/seam-ltm/`; patches load it with `[declare -stdpath seam-ltm]`.
- Parameter ranges and defaults = the VST's: divergence 0…360° (default 90), yaw/pitch/roll −180…180° (default 0), A1/A2/A3 −12…+12 dB (default 0). Out-of-range values are clipped.
- Parameter ramp **20 ms** linear (`Seam::LinearRamp`, `seam_ramp.h`) — deliberate departure from the VST (no ramp); steady state identical.
- Engine post-Haar stage computes in `double` (spec leaves the type to the builder; double is closest to the VST's 64-bit path and within float tolerance of its 32-bit path).
- Bamodulex driver order LFU, RFD, RBU, LBD; the `stone` is upright.
- Hand-written C++; no `faust2puredata` output ships. Code, comments, commits in English.
- **Di Scipio's patch never enters the repo.** `~/Downloads/UPU/TIMPnew.pd/` is never modified.
- Builds: tests and plugins use the existing Xcode tree `build` with `--config Release` (`-DSEAM_VST3SDK_DIR=/Users/giuseppe/Documents/github/seam/sdk/vst3sdk` is already in its cache).

## Review Focus

1. **Pd in-place buffers** — Pd may hand an outlet the same memory as an inlet; output must equal the separate-buffer result. → aliasing test in Tasks 3, 5, 6.
2. **DSP restarted mid-rehearsal** (DSP off/on, or a rate change) — no ramp left half-way, no stale Haar state; the next block starts at the targets. → prepare-snaps test in Task 3.
3. **Bad input from the patch** — too many/too few/non-numeric creation arguments, out-of-range or unknown messages — never a crash, always a sensible value and a console line. → args/clip/unknown tests in Tasks 3 and 5, smoke patch in Task 4.
4. **Wrong sample rate** — Pd started at 44.1 kHz: a visible warning. → engine test in Task 3, smoke run at `-r 44100` in Task 4.
5. **Silent routing loss in the concert patch** — `stone-out.pd` not open (`throw~ … no matching catch`) or Pd audio set to fewer than 12 outputs (Pd drops channels silently). → smoke run in Task 7 greps for `no matching catch`; README states the 12-output setting.

---

### Task 1: Extract `m2xhgr_dsp.h` (VST3 bit-identical)

**Files:**
- Create: `plugins/m2xhgr/source/m2xhgr_dsp.h`
- Modify: `plugins/m2xhgr/source/m2xhgr_processor.h` (member `HaarState<double> haar` → `Seam::m2xhgr::Bank bank`), `plugins/m2xhgr/source/m2xhgr_processor.cpp::processHaarMono`
- Modify: `plugins/lr2xhgr/source/lr2xhgr_processor.h` (`haarL`, `haarR` → two `Seam::m2xhgr::Bank`), `plugins/lr2xhgr/source/lr2xhgr_processor.cpp::processHaarStereo` (the two inline Haar blocks, lines ~240–256), plus the `reset()` calls wherever `haar*.reset()` appears
- Modify: `plugins/lr2xhgr/CMakeLists.txt` (add `../m2xhgr/source` to include dirs)
- Test: `tests/m2xhgr_dsp_test.cpp`, `tests/CMakeLists.txt` (new target, includes `plugins/m2xhgr/source` and `plugins/_common`; also add `plugins/m2xhgr/source` to `lr2xhgr_dsp_test`)

**Interfaces:**
- Produces, in `namespace Seam { namespace m2xhgr {`:
  - `struct Bank { HaarState<double> haar; void reset(); template <typename T> void decompose (T x, T a[4]); };` — Haar in `double`, outputs cast to `T` (exactly the cast sequence in today's `processHaarMono`).
  - `template <typename T> inline void tick (Bank& b, T x, T g1, T g2, T g3, T yaw, T pitch, T roll, T out[4]);` — `decompose` then `gainRotateYPR`. Angles in radians, gains linear.
- Header opens with the FAUST REFERENCE block moved from `m2xhgr_processor.h` (`m2xhgr(g1,g2,g3) = sdw.haarmn(1) : hgain(g1,g2,g3);`).

- [ ] **Step 1: Branch**

Run: `git switch -c pd-stone`

- [ ] **Step 2: Write the failing test** `tests/m2xhgr_dsp_test.cpp`

The test carries a verbatim copy of today's per-sample body of `processHaarMono` as `referenceTick<T>(HaarState<double>&, …)` and compares:
- `TEST_CASE("m2xhgr tick: bit-identical to the pre-refactor processor, double")` — 4096 samples of a fixed-seed LCG noise in [−1, 1), parameters yaw 0.7, pitch −0.3, roll 1.9 rad, gains `db2lin(-3)`, `db2lin(2)`, `db2lin(6)`; `CHECK(out[k] == ref[k])` for every sample and channel (exact equality).
- Same case with `T = float` (exact equality).
- `TEST_CASE("m2xhgr Bank::reset clears the Haar memory")` — after a non-zero sample and `reset()`, `decompose(0.0)` yields all zeros.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --config Release --target m2xhgr_dsp_test`
Expected: compile error, `m2xhgr_dsp.h` not found.

- [ ] **Step 4: Implement `m2xhgr_dsp.h`**, then switch both processors to it

`processHaarMono`'s loop body becomes one `Seam::m2xhgr::tick` call; `processHaarStereo` calls `bankL.decompose(inL[i], la)` and `bankR.decompose(inR[i], ra)` before the existing `lr2xhgr::mix`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build --config Release --target m2xhgr_dsp_test lr2xhgr_dsp_test && ctest --test-dir build -C Release -R "m2xhgr_dsp_test|lr2xhgr_dsp_test" --output-on-failure`
Expected: both PASS.

- [ ] **Step 6: Mutation check**

Swap `a[1]` and `a[2]` in `Bank::decompose`, rerun Step 5: expect FAIL. Revert, rerun: PASS.

- [ ] **Step 7: Plugins still build and validate**

Run: `cmake --build build --config Release --target m2xhgr lr2xhgr && build/bin/Debug/validator build/VST3/Release/m2xhgr.vst3 && build/bin/Debug/validator build/VST3/Release/lr2xhgr.vst3`
Expected: builds succeed; validator reports 0 failed tests for each.

- [ ] **Step 8: Commit**

```bash
git add plugins/m2xhgr plugins/lr2xhgr tests/m2xhgr_dsp_test.cpp tests/CMakeLists.txt
git commit -m "refactor(m2xhgr): extract the Haar bank into m2xhgr_dsp.h; lr2xhgr reuses it"
```

---

### Task 2: Extract `bamodulex_dsp.h` (VST3 bit-identical)

**Files:**
- Create: `plugins/bamodulex/source/bamodulex_dsp.h`
- Modify: `plugins/bamodulex/source/bamodulex_processor.cpp::processMatrix` (loop body → `decode`); the matrix comment and `kHalf` rationale move to the header
- Test: `tests/bamodulex_dsp_test.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `namespace Seam { namespace bamodulex { template <typename T> inline void decode (T w, T y, T z, T x, T out[4]); } }` — `out` = LFU, RFD, RBU, LBD; ½ normalisation.

- [ ] **Step 1: Write the failing test** `tests/bamodulex_dsp_test.cpp`
  - `TEST_CASE("bamodulex decode: matrix rows")` — `decode(1,0,0,0)` → all 0.5; `decode(0,1,0,0)` → {0.5,−0.5,−0.5,0.5}; `decode(0,0,1,0)` → {0.5,−0.5,0.5,−0.5}; `decode(0,0,0,1)` → {0.5,0.5,−0.5,−0.5}.
  - `TEST_CASE("bamodulex decode: bit-identical to the pre-refactor processor")` — verbatim copy of today's four lines as reference, 4096 LCG frames, float and double, exact equality.
  - `TEST_CASE("bamodulex decode: orthogonal — energy preserved")` — Σout² == Σin² (Approx) for random frames.
- [ ] **Step 2: Run to verify it fails** — `cmake -S . -B build && cmake --build build --config Release --target bamodulex_dsp_test` → header not found.
- [ ] **Step 3: Implement `decode`** and call it from `processMatrix`.
- [ ] **Step 4: Run** — `ctest --test-dir build -C Release -R bamodulex_dsp_test --output-on-failure` → PASS.
- [ ] **Step 5: Mutation check** — flip the sign of `z` in the RBU row: FAIL; revert: PASS.
- [ ] **Step 6: Plugin builds and validates** — `cmake --build build --config Release --target bamodulex && build/bin/Debug/validator build/VST3/Release/bamodulex.vst3` → 0 failed.
- [ ] **Step 7: Commit** — `git commit -m "refactor(bamodulex): extract the decoder matrix into bamodulex_dsp.h"`

---

### Task 3: `m2xhgr~` engine and the shared parameter helper

**Files:**
- Create: `pd/seam_pd_param.h`, `pd/m2xhgr_engine.h`
- Test: `tests/pd_m2xhgr_engine_test.cpp`, `tests/CMakeLists.txt` (target `pd_m2xhgr_engine_test`, includes `pd/`, `plugins/m2xhgr/source`, `plugins/_common`)

**Interfaces:**
- Consumes: `Seam::m2xhgr::Bank`, `Seam::m2xhgr::tick` (Task 1); `Seam::LinearRamp`; `seam::meter::db2lin`.
- Produces, `namespace Seam { namespace pd {`:
  - `constexpr double kRampSeconds = 0.020; constexpr double kNominalRate = 48000.0;`
  - `struct RampedParam { const char* name; double lo, hi, deflt; LinearRamp ramp; void init (double v); void set (double v, double fs); double next(); double target() const; double current() const; };` — `init` clips and snaps; `set` clips and starts a `kRampSeconds` ramp; `snap()` via `prepare`.
  - `std::string formatObjectLine (const char* cls, const RampedParam* p, int n);` — `"<cls> v1 v2 …"`, each value `%g` of the **target**.
  - `class M2xhgrEngine { public: M2xhgrEngine (const double* args, int nargs); void prepare (double fs); bool rateIsNominal() const; bool set (const char* name, double value); void process (const float* in, float* const out[4], int n); std::string objectLine() const; double current (int index) const; };` — parameter order and message names: `yaw pitch roll a1 a2 a3` (index 0…5). `prepare` stores `fs`, resets the bank, snaps every ramp. `set` returns false for an unknown name. `process` advances each ramp per sample, converts degrees → radians and dB → linear, reads the input sample into a local before writing any output (in-place safe), computes in `double`.

- [ ] **Step 1: Write the failing tests** `tests/pd_m2xhgr_engine_test.cpp`
  - `"defaults and creation args"` — `M2xhgrEngine(nullptr,0).objectLine() == "m2xhgr~ 0 0 0 0 0 0"`; args `{10, 20}` → `"m2xhgr~ 10 20 0 0 0 0"`; 8 args → extra ignored, first six used.
  - `"clipping"` — `set("yaw", 270)` → line shows `180`; `set("a1", 40)` → `12`; `set("a3", -99)` → `-12`; creation arg yaw −500 → `-180`.
  - `"unknown message"` — `set("divergence", 10) == false` and the line is unchanged.
  - `"equivalence with the plugin DSP at steady state"` — args `{30, -15, 45, -3, 2, 6}`, `prepare(48000)`, 4096 LCG samples; reference = `Seam::m2xhgr::tick<double>` with the same values in radians/linear; `CHECK(out == Approx(ref).epsilon(1e-6))` every sample/channel.
  - `"ramp: reaches the target in 20 ms, not before"` — `prepare(48000)`, `set("yaw", 90)`, process 959 samples → `current(0) < 90`; one more → `current(0) == 90`.
  - `"prepare snaps a pending ramp"` (Review Focus 2) — `set("yaw", 90)`, process 10 samples, `prepare(48000)` → `current(0) == 90`.
  - `"in-place buffers"` (Review Focus 1) — same input processed with `out[0]` aliasing `in` and with separate buffers on two fresh engines → identical outputs.
  - `"rate check"` (Review Focus 4) — `prepare(48000)` → `rateIsNominal()`; `prepare(44100)` → false.
- [ ] **Step 2: Run to verify it fails** — `cmake -S . -B build && cmake --build build --config Release --target pd_m2xhgr_engine_test` → headers not found.
- [ ] **Step 3: Implement `seam_pd_param.h` and `m2xhgr_engine.h`** to the interfaces above; header comment cites the spec and `m2xhgr_dsp.h`.
- [ ] **Step 4: Run** — `ctest --test-dir build -C Release -R pd_m2xhgr_engine_test --output-on-failure` → PASS.
- [ ] **Step 5: Mutation checks** — (a) write `out[0]` before reading `in[i]` into a local: the in-place test FAILs; (b) set `kRampSeconds` to 0: the ramp test FAILs. Revert each, PASS.
- [ ] **Step 6: Commit** — `git commit -m "feat(pd): m2xhgr~ engine with ramped, clipped parameters"`

---

### Task 4: `m2xhgr~` in Pd — glue, Makefile, help patch, smoke run

**Files:**
- Create: `pd/m2xhgr~.cpp`, `pd/Makefile`, `pd/m2xhgr~-help.pd`, `pd/tests/smoke-m2xhgr.pd`, `pd/README.md`, `pd/.gitignore` (`*.d_fat`)

**Interfaces:**
- Consumes: `Seam::pd::M2xhgrEngine` (Task 3).
- Produces: Pd class `m2xhgr~`, setup symbol `extern "C" void m2xhgr_tilde_setup(void)`; 1 signal inlet (`CLASS_MAINSIGNALIN`), 4 signal outlets W Y Z X; methods `dsp`, `print`, and `anything` (selector = parameter name, first float = value). Makefile targets `all`, `install`, `clean`, `smoke`, one rule per external (`<name>~.d_fat` from `<name>~.cpp`), variables `PDINCLUDEDIR`, `PDBIN` (`/Applications/Pd-0.56-2.app/Contents/Resources/bin/pd`), `INSTALLDIR` (`$(HOME)/Documents/Pd/externals/seam-ltm`).

- [ ] **Step 1: Write the smoke patch** `pd/tests/smoke-m2xhgr.pd`

Contents: `[m2xhgr~ 10 20 0 0 0 0]` (named so `print` reaches it via a message box connected to its left inlet), a second `[m2xhgr~ foo 1 2 3 4 5 6 7]` (bad args), `[loadbang] → [t b b b]` that sends `print`, `a1 40`, `print`, `nonsense 3` to the first instance, `[; pd dsp 1(`, then `[del 200] → [; pd quit(`.

- [ ] **Step 2: Run to verify it fails** — `make -C pd smoke`
Expected: FAIL (`m2xhgr~ … couldn't create`, or make error: no rule).

The `smoke` target runs, for each `tests/smoke-*.pd`: `$(PDBIN) -nogui -noprefs -nomidi -noaudio -stderr -r 48000 -path . -open tests/smoke-<x>.pd 2>&1 | tee tests/smoke-<x>.log` and fails if the log contains `couldn't create`.

- [ ] **Step 3: Implement `m2xhgr~.cpp` and the Makefile**

Glue holds `Seam::pd::M2xhgrEngine* engine` (allocated in `new`, deleted in `free`), collects only `A_FLOAT` creation arguments (a symbol argument posts `pd_error(x, "m2xhgr~: ignoring non-numeric argument '%s'")`). `dsp`: `engine->prepare(sp[0]->s_sr)`; if `!rateIsNominal()` post `m2xhgr~: running at %g Hz — the Haar band mapping is tuned for 48000 Hz`; `dsp_add` the perform routine. `anything`: `set` with the first float; on false or missing float, `pd_error(x, "m2xhgr~: no method for '%s'")`. `print`: `post("%s", engine->objectLine().c_str())`. `static_assert(sizeof(t_sample) == sizeof(float))`. Compile flags: `-std=c++17 -O2 -arch x86_64 -arch arm64 -mmacosx-version-min=11.0 -bundle -undefined dynamic_lookup -I$(PDINCLUDEDIR) -I. -I../plugins/_common -I../plugins/m2xhgr/source`.

- [ ] **Step 4: Run the smoke test**

Run: `make -C pd && make -C pd smoke && cat pd/tests/smoke-m2xhgr.log`
Expected: PASS; log contains `m2xhgr~ 10 20 0 0 0 0`, then `m2xhgr~ 10 20 0 12 0 0`, `no method for 'nonsense'`, `ignoring non-numeric argument 'foo'`; no `couldn't create`.

- [ ] **Step 5: Rate warning** (Review Focus 4)

Run: `/Applications/Pd-0.56-2.app/Contents/Resources/bin/pd -nogui -noprefs -nomidi -noaudio -stderr -r 44100 -path pd -open pd/tests/smoke-m2xhgr.pd 2>&1 | grep "tuned for 48000"`
Expected: one matching line.

- [ ] **Step 6: Help patch** `pd/m2xhgr~-help.pd`

Contents: title and one-paragraph description (mono → AmbiX by Haar decomposition, 48 kHz note, creation-argument order); input section `[adc~ 1]` and `[noise~] → [*~ 0.1]` selected by a toggle-driven `[*~]` pair; six `hsl` sliders with number boxes — yaw/pitch/roll range −180…180, a1/a2/a3 range −12…12 — each into a message box `[yaw $1(` … `[a3 $1(` into the object; a `[print(` message box; per outlet `[env~ 1024] → [- 100] → [vu]` labelled W, Y, Z, X; outlets also into `[dac~ 1 2 3 4]` behind a `[*~]` mute toggle (default off). Opens in Pd 0.56 with no errors: verify with `pd -nogui -noprefs -noaudio -stderr -path pd -open pd/m2xhgr~-help.pd -send "pd quit"` → no `couldn't create`, no `error`.

- [ ] **Step 7: README** `pd/README.md` — what the three externals are (one line each, linked to their plugins), build (`make -C pd`), install (`make -C pd install`), the `[declare -stdpath seam-ltm]` line, the 48 kHz constraint, and the smoke test. Add a line to the root `README.md`? **No** — the README is generated from `doc/plugins.toml`; leave it alone.
- [ ] **Step 8: Commit** — `git add pd && git commit -m "feat(pd): m2xhgr~ external, Makefile, help patch and smoke test"`

---

### Task 5: `lr2xhgr~` — engine, glue, help patch

**Files:**
- Create: `pd/lr2xhgr_engine.h`, `pd/lr2xhgr~.cpp`, `pd/lr2xhgr~-help.pd`, `pd/tests/smoke-lr2xhgr.pd`
- Modify: `pd/Makefile` (rule + `-I../plugins/lr2xhgr/source -I../plugins/m2xhgr/source`)
- Test: `tests/pd_lr2xhgr_engine_test.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Seam::m2xhgr::Bank` (Task 1), `Seam::lr2xhgr::mix` (existing), `RampedParam` / `formatObjectLine` / constants (Task 3).
- Produces: `class Seam::pd::Lr2xhgrEngine` with the same public surface as `M2xhgrEngine` except `void process (const float* inL, const float* inR, float* const out[4], int n);`. Parameter order and names: `divergence yaw pitch roll a1 a2 a3` (index 0…6), divergence default 90, range 0…360. The value handed to `mix` as `divergence` is **half** the parameter, in radians (as `lr2xhgr_processor.cpp` line 133 does). Pd class `lr2xhgr~`, setup `lr2xhgr_tilde_setup`, 2 signal inlets (L main, R via `inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal)`), 4 signal outlets.

- [ ] **Step 1: Failing tests** `tests/pd_lr2xhgr_engine_test.cpp` — the Task 3 cases adapted (defaults line `"lr2xhgr~ 90 0 0 0 0 0 0"`; divergence 400 → `360`, −5 → `0`; `set("foo",1) == false`), plus:
  - `"equivalence with the plugin DSP"` — args `{35, 10, -20, 5, -2, 0, 3}`, two independent LCG streams for L and R; reference = two `Bank::decompose<double>` + `lr2xhgr::mix<double>` with `divergence = 35/2 °` in radians; Approx epsilon 1e-6.
  - `"in-place buffers"` — `out[0]` aliasing `inL` and `out[1]` aliasing `inR` gives the separate-buffer result.
- [ ] **Step 2: Run to verify it fails** — `cmake --build build --config Release --target pd_lr2xhgr_engine_test` → header not found.
- [ ] **Step 3: Implement `lr2xhgr_engine.h`**, then `lr2xhgr~.cpp` (same glue pattern as Task 4; messages `divergence yaw pitch roll a1 a2 a3`).
- [ ] **Step 4: Run** — `ctest --test-dir build -C Release -R pd_lr2xhgr_engine_test --output-on-failure` → PASS; `make -C pd && make -C pd smoke` → PASS, `smoke-lr2xhgr.log` contains `lr2xhgr~ 35 0 0 0 -2 0 0` for a smoke patch created with `[lr2xhgr~ 35 0 0 0 -2 0 0]` and sent `print`.
- [ ] **Step 5: Mutation check** — pass the full divergence (not half) to `mix`: the equivalence test FAILs; revert: PASS.
- [ ] **Step 6: Help patch** `pd/lr2xhgr~-help.pd` — as Task 4 Step 6 but stereo input (`[adc~ 1 2]` / two `[noise~]`), seven sliders (divergence 0…360 first), and the description of divergence (L at +div/2, R at −div/2). This is the rehearsal tool: the `[print(` box sits next to the sliders, labelled "copy the console line into stone-out.pd". Same open-without-errors check.
- [ ] **Step 7: Commit** — `git commit -m "feat(pd): lr2xhgr~ external and rehearsal help patch"`

---

### Task 6: `bamodulex~` — engine, glue, help patch

**Files:**
- Create: `pd/bamodulex_engine.h`, `pd/bamodulex~.cpp`, `pd/bamodulex~-help.pd`, `pd/tests/smoke-bamodulex.pd`
- Modify: `pd/Makefile`
- Test: `tests/pd_bamodulex_engine_test.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Seam::bamodulex::decode` (Task 2).
- Produces: `namespace Seam { namespace pd { inline void bamodulexBlock (const float* const in[4], float* const out[4], int n); } }` — reads the four inputs of a frame into locals before writing (in-place safe). Pd class `bamodulex~`, setup `bamodulex_tilde_setup`, 4 signal inlets, 4 signal outlets, no parameters; creation arguments ignored with one `pd_error`; `print` posts `bamodulex~`; no rate warning, because the matrix is rate-independent.

- [ ] **Step 1: Failing tests** — `"block equals decode"` (1024 LCG frames vs `decode<float>`, exact), `"in-place: every out[k] aliasing in[k]"` (equal to separate buffers), `"in-place: out[0] aliasing in[3]"` (equal to separate buffers).
- [ ] **Step 2: Run to verify it fails** — target `pd_bamodulex_engine_test` → header not found.
- [ ] **Step 3: Implement** `bamodulex_engine.h` and `bamodulex~.cpp`.
- [ ] **Step 4: Run** — ctest PASS; `make -C pd smoke` PASS.
- [ ] **Step 5: Mutation check** — write `out[k]` inside the read loop (no locals): an in-place test FAILs; revert: PASS.
- [ ] **Step 6: Help patch** `pd/bamodulex~-help.pd` — the matrix written out (LFU = (W+Y+Z+X)/2, RFD = (W−Y−Z+X)/2, RBU = (W−Y+Z−X)/2, LBD = (W+Y−Z−X)/2), driver order, upright `stone`; example chain `[noise~] → [lr2xhgr~] → [bamodulex~]` with four `vu` meters labelled LFU RFD RBU LBD. Same open-without-errors check.
- [ ] **Step 7: Commit** — `git commit -m "feat(pd): bamodulex~ external and help patch"`

---

### Task 7: Install and the concert patch (outside the repo)

**Files (none in the repo except the Makefile check):**
- Create: `~/Downloads/UPU/TIMPnew-stone/` (copy of `~/Downloads/UPU/TIMPnew.pd/`), `~/Downloads/UPU/TIMPnew-stone/stone-throw.pd`, `~/Downloads/UPU/stone-out.pd`, `~/Downloads/UPU/stone-bypass.pd`, `~/Downloads/UPU/LEGGIMI-stone.txt`
- Modify: `~/Downloads/UPU/TIMPnew-stone/processTR2.pd` only

**Interfaces:**
- Consumes: the three installed externals.
- Produces: `[stone-throw <name>]` abstraction — 2 signal inlets → `throw~ $1L`, `throw~ $1R` (same inlet count as `dac~ 5 6`, so every `#X connect` line stays valid). Names `upu1` (was 5-6), `upu2` (was 7-8), `upu3` (was 9-10).

- [ ] **Step 1: Install** — `make -C pd install && ls ~/Documents/Pd/externals/seam-ltm/` → three `.d_fat` and three `-help.pd`.
- [ ] **Step 2: Copy** — `cp -R ~/Downloads/UPU/TIMPnew.pd ~/Downloads/UPU/TIMPnew-stone` (the original is only read).
- [ ] **Step 3: Replace the eight `dac~`** in the copy's `processTR2.pd`: ` dac~ 5 6;` → ` stone-throw upu1;`, ` dac~ 7 8;` → ` stone-throw upu2;`, ` dac~ 9 10;` → ` stone-throw upu3;`.
Verify: `grep -c "stone-throw" …/TIMPnew-stone/processTR2.pd` → `8`; `grep -c "dac~" …/TIMPnew-stone/processTR2.pd` → `0`; `diff ~/Downloads/UPU/TIMPnew.pd ~/Downloads/UPU/TIMPnew-stone` → only `processTR2.pd`, only those 8 lines, and `stone-throw.pd` only in the copy.
- [ ] **Step 4: Write `stone-throw.pd`, `stone-out.pd`, `stone-bypass.pd`**
  - `stone-out.pd`: `[declare -stdpath seam-ltm]`; for n = 1…3: `[catch~ upu<n>L]`, `[catch~ upu<n>R]` → `[lr2xhgr~ 90 0 0 0 0 0 0]` → `[bamodulex~]` → `[dac~ 1 2 3 4]` / `[dac~ 5 6 7 8]` / `[dac~ 9 10 11 12]`; a comment per group ("rehearsal values go in the lr2xhgr~ box; channels in the dac~ box"); one `[print(` box per `lr2xhgr~`.
  - `stone-bypass.pd`: the same `catch~` pairs straight into `[dac~ 5 6]`, `[dac~ 7 8]`, `[dac~ 9 10]` — the original routing, for an A/B by ear. Opened **instead of** `stone-out.pd`, never together (`catch~` names must be unique).
- [ ] **Step 5: Smoke run of the concert set** (Review Focus 5)

Run: `/Applications/Pd-0.56-2.app/Contents/Resources/bin/pd -nogui -noprefs -nomidi -noaudio -stderr -r 48000 -open ~/Downloads/UPU/TIMPnew-stone/TIMP.pd -open ~/Downloads/UPU/stone-out.pd -send "pd dsp 1" -send "pd quit" 2>&1 | grep -E "couldn't create|no matching catch|tuned for"`
Expected: no output. Then repeat with `stone-bypass.pd` in place of `stone-out.pd`: no output. Then open `TIMP.pd` alone: `no matching catch` lines appear (confirms the check would catch a missing `stone-out.pd`).
- [ ] **Step 6: `LEGGIMI-stone.txt`** (Italian, Giuseppe's own folder): open `TIMPnew-stone/TIMP.pd` **and** `stone-out.pd` (or `stone-bypass.pd` for A/B, never both); Pd audio settings 48 kHz, **12 output channels**; do not open `simpSamp*.pd`; where to put the rehearsal values; the original stays in `TIMPnew.pd/`.
- [ ] **Step 7: Commit** (repo only — nothing from `~/Downloads/UPU`) — if Task 7 changed nothing in the repo, skip.

---

### Task 8: Rehearsal checklist (with Giuseppe, at the Big Six and the stone)

Not automated; run with Giuseppe and log the results in `pd/README.md` under "Field notes".

- [ ] Pd at 48 kHz, 12 outputs; no warnings in the console.
- [ ] `stone-bypass.pd`: the piece sounds as in rehearsal with Di Scipio (null by ear).
- [ ] `stone-out.pd`: every group reaches all four drivers; LFU of each group lands on the same driver through cues 1-2 / 3-4.
- [ ] Find divergence / rotation / trims per group with `lr2xhgr~-help.pd` or directly in `stone-out.pd`; `print`, paste, save.
- [ ] Reopen `stone-out.pd` from disk: the pasted values recreate the sound.
- [ ] Commit the field notes.
