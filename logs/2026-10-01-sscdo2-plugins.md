# Session log — 2026-10-01 — SSCDO#2: the C++ plugins, LMO first

## Goal
The Faust side of LMO, delRM and stunedrev is closed (`logs/2026-09-29-sscdo2-ricognizione.md`); this session starts the hand-written C++ plugins in `seam-ltm`.
Giuseppe's general rules for the whole phase: double precision, a window coherent with the suite, SEAM code reused where it exists and new code written to be reused.
Spec `docs/superpowers/specs/2026-10-01-lmo-plugin-design.md`, plan `docs/superpowers/plans/2026-10-01-lmo-plugin.md`, branch `lmo-plugin`.

## Decisions

### The sample-rate rule (Giuseppe)
In this project every process sounds as it does at 96 kHz, whatever rate the session runs at.
Times and frequencies are designed at the session's rate; a quantity that a white source makes rate-dependent is anchored at 96 kHz, as `sdt.delrmint` already was.

### stunedrev: the reading of 2026-09-29 stands (Giuseppe)
The rule seemed to clash with the decision that each rate has its own set of primes.
They agree: the delays are milliseconds converted at the session's rate, so the time of every section and the memory of every line are those of 96 kHz, to within the step to the next prime; only the primes themselves differ, which is the incommensurability the system wants.
The delays are not computed at 96 kHz and converted back (at 48 kHz they would stop being prime).

### Order
LMO first, as the smaller plugin and the one whose reusable block (an order-N Butterworth in SVF sections) the choir will need too; stunedrev second.

### The glissando inside the plugin (Giuseppe, option C)
The cues of the patch send a target and a time to `line`.
The plugin has both: f, and glide, the time of the next move of f, linear in Hz, with `line`'s semantics (a new target starts from where the ramp is).
glide = 0 means a 25 ms ramp, which turns host automation into a continuous curve; so the cue layer, built last in Reaper, may use either automation or commands.
A value of f sent again does not restart a glissando: Reaper re-sends automation every block, and without this guard a 120 s glissando would never arrive.
glide is applied before f in every block, so a cue sets glide, then f.

### Δ on the short ramp
Approved in conversation as following glide; changed in the spec, with Giuseppe's review, to the 25 ms ramp: Δ is a hand control, and with glide at 120 s for cue 2 a hand move of Δ would take two minutes.

### The density anchor (`sdt.lmodens`)
White noise of constant RMS spreads its power over SR/2: a band of fixed width is 3.01 dB louder at 48 kHz than at 96 kHz.
The LMO's output is multiplied by √(SR/96000), unity at 96 kHz, in the specification (`sdt.lmoosc`, `sdt.lmo`, faust-libraries d355254) and in the C++.
The `lmo-beats` prototype follows it (77ea332); at 48 kHz, the study's rate, its absolute levels moved by −3.01 dB (−45.3 to −48.3 dBFS), every relative number unchanged, self-test OK.
Its renders are kept as heard on 2026-09-29: they are normalised to −20 dBFS, so the anchor would change only the gain recorded in `render-log.md`.
Measured on the plugin: the band energy holds within 0.0001 dB at 97.44 Hz across 44.1, 48 and 192 kHz; at 1 kHz and 44.1 kHz it is 0.0117 dB off, the bilinear transform's warping (prewarped at the centre only), not the anchor.

### The low band held at 1 Hz or above
With f at 20 Hz and Δ at 50 Hz the low band of channel 0 would sit at −5 Hz.
Unclamped, the filters diverge to inf and NaN (seen as a mutation of the test).
Both the specification and the C++ hold the centre at `max(1, f − Δ/2 + i)`.

### Name, identity, window
`lmo`, as the specification; FUID `0x5E4D0010`, the 17th plugin; format S (300 px), POWER in OPS, f, glide, Δ, volume in FINE, "f now" in FOOTER; no SETUP zone, LMO has no station identity.
Four channels on an `kAmbi1stOrderACN` bus, with a declared input that is never read, so that the host does not route around the insert; subcategory `Fx|Generator`.

### Reusable headers in `plugins/_common/`
- `seam_noise.h`, `Seam::FaustMultinoise`: `no.multinoise(N)` bit for bit. One LCG stepped N times per sample; stream k is step N−1−k, and the last step is the one fed back, both read from the code Faust generates. The seed is a constructor argument, for the choir, whose four instances share it in the original (a question for Davide).
- `seam_butterworth.h`, `Seam::ButterworthSVF<N>`: J.O. Smith's SVF sections (faustlibraries 0965ea2). The damping of each section depends only on N, so a frequency change costs one tangent and N/2 reciprocals. Even orders only, a departure from the spec's "N = 1, 2, 3": no plugin needs odd orders yet.
- `seam_ramp.h`, `Seam::LinearRamp`: `line`'s semantics in seconds, landing exactly on the target. Not in the spec; written as reusable because every SSCDO#2 plugin has ramps.

## Verification
The Faust references are rendered by `doc/study/sscdo2/lmo-plugin/gen-ref.sh` into `tests/ref/lmo_ref.h`, so the tests need no `faust` binary.
The noise is identical to Faust bit for bit; the filters agree to 1e-12 of the peak; the engine agrees with `sdt.lmo(4, 97.44, 20)` to 7.9e-14 of the peak at 96 kHz and 5.2e-14 at 48 kHz.
The first comparison, over the first 2048 samples, failed at 1.1e-11: the band had not formed yet (peak 3.1e-7), and rounding was weighed against almost nothing; the references now skip one second.
Level at 1 kHz over 20 s: −38.466 dBFS at 48 kHz, −38.428 at 96 kHz, −38.439 at 44.1 kHz.
Every test was verified by mutation (`doc/study/sscdo2/lmo-plugin/mutations.md`); one mutation stayed green by nature, reversing the order of the filter sections, since sections in series commute.
VST3 validator: 47 of 47.
The suite's `minos_lint` fails in `build-test` for a reason older than this branch: that tree's cache holds a deployment target of 15.7, from before the 11.0 floor; `build`, which makes the plugins, is at 11.0.

## Final review
A fresh reviewer read the whole branch and found two important defects in the processor, both fixed with a test that failed first.
- The processor wrote the SDK's parameter objects from the audio thread (`setParamNormalized` in `process()`), which notifies the open editor synchronously: a lock and VSTGUI redraws off the main thread, at every automated block, with the window open on stage. The controls now live in atomics (`plugins/lmo/source/lmo_params.h`, `lmo::ParamBox`), as in multipink and ltglide; `process()` never touches a `Parameter`.
- `setState` stored f before glide: a block landing between the two stores started the recalled f on the old glide, 120 s instead of 25 ms. A recall now stores glide first (`lmo::kRecallOrder`); `lmo_params_test` runs a block at every interleaving of the five stores and failed, before the fix, exactly after the f store.
A recall while LMO plays moves f over the recalled glide, as a cue does (documented in `plugins/lmo/doc/README.md`, with the advice to draw sloped f envelopes with glide at 0).

## Open
- stunedrev: done below; then the choir.

## Host check, listening and registry
Giuseppe listened in Reaper at 96 kHz (POWER and volume fades, cue 1, cue 2 with glide 120 then f 112.67, a Δ sweep, the same session at 48 kHz): listening OK.
Giuseppe loaded LMO in Reaper and sent the window's screenshot (`docs/img/lmo.png`): the window is as designed, the bus is 4 in + out, and the read-only "f now" reaches the footer (440.00 with f at 440), which the final reviewer had left to the host check.
The registry gains a family for the works, "Works — SSCDO#2", with LMO; the counts of `doc/scripts/test-doc.sh`, `render-readme.py` and `CLAUDE.md` go from sixteen to seventeen; `make -C doc test` passes its 12 checks, `uidesc_lint_selftest` passes with the screenshot.
- Then stunedrev (below).

# stunedrev

Spec `docs/superpowers/specs/2026-10-01-stunedrev-plugin-design.md`, plan `docs/superpowers/plans/2026-10-01-stunedrev-plugin.md`, branch `stunedrev-plugin`, executed inline.

## Decisions (Giuseppe, brainstorming)
- The APF INPUT and APF OUTPUT faders (CC83, CC84) live in the plugin, linear 0–1 with 25 ms ramps, as LMO's volume.
- A change of time makes the 42 delays jump, as the specification and the original do; no crossfade, which would break the all-pass during the fade.
- RESET in OPS, emptying the memory while playing; POWER added for the suite standard (the lines keep running while it is off).
- The footer shows each line's energy centroid, the sum of its 42 delays: the report's card `stunedrev-tempi`, live.
- One arena for the 168 sections, each sized exactly for its longest delay, allocated and zeroed in `setActive`; `sdt.stmd`'s +150 is Faust's compile-time margin and stays in the specification.
- Filters are reusable C++ libraries, as in Faust: the all-pass is `plugins/_common/seam_moorer.h` (`Seam::MoorerAllpass`, `sjm.apfv` with g and the buffer as parameters), not a struct of the plugin. With `seam_primes.h`, the sieve behind `sff.np`, the port adds two libraries to `_common/`.

## Decisions taken while designing and building
- RESET is a GUI-only button that increments a generation counter in the engine: a momentary parameter would be lost in a host that merges press and release (ltglide, Reaper). A click during the clearing restarts it; a click before activation is dropped.
- The clearing zeroes 16 KiB per sample of the block, not 4 MiB per block as first designed: with 32-sample blocks 4 MiB would outlast the block. It lasts 0.39 s at any block size and rate, 0.418 s with the fade.
- Test 4 of the spec runs on a 50 ms noise burst instead of the clarinet note: a unit test cannot embed a 3.8 s WAV; the clarinet stays for the listening.
- The processor's state round-trip is tested on its codec (`stunedrev_state.h`), which the processor calls as it is, with the SDK's memory stream.
- A host value between two time steps maps to the millisecond the SDK's `RangeParameter` displays (`int(norm·100)`), not to a rounding of `norm·99`.

## Verification
- The 16 800 delays equal `sdt.stdel` exactly at 96 and 48 kHz, with the product in the order Faust writes it.
- `seam_moorer.h` equals `sjm.apfv` for g = 1/√2 and 0.7 below 1e-15 of the peak, and carries unit energy for four gains.
- The engine equals `sdt.stunedrev(83, 47, 7, 71)` over 30 s to 1.83e-15 of the peak at 96 kHz and 1.62e-15 at 48 kHz, and to 8.7e-16 across a change of time (t e from 7 to 9 ms at a block boundary).
- The arena at 96 kHz is 77 085 940 doubles, 588.119 MiB, as computed on 2026-09-29; 1.15 GiB at 192 kHz.
- Cost: 9.4 % of a core at 96 kHz, 18.8 % at 192 kHz (Intel i7-8850H).
- 23 mutations (`doc/study/sscdo2/stunedrev-plugin/mutations.md`); three survived the first run and each was a hole in a test: an impulse response too sparse to see a state left by `clear()`, a lower bound too loose to tell a restarted RESET from the end of the first, a check placed after the replayed RESET had already ended. The tests were tightened and the mutants run again: all RED.
- A trap of the method, now in `mutate.py`: restoring the source leaves the mutated binary in the build tree, and the next ctest runs it; the script rebuilds every test it touched.
- VST3 validator: 47 of 47. `tools/check-uidesc.py`: no error. The whole ctest passes but `uidesc_lint_selftest`, which waits for the screenshot.

## Final review (fresh reviewer, whole branch)
No critical finding; the DSP, the memory lifetime, the threading and the six edge cases of the host held.
- Important, fixed: the lines lose no energy, so after a burst their silent tails sink into subnormal numbers and stay in the 588 MiB; measured by the reviewer at 48 kHz, the cost went from 4.7 % to 15 % of a core in 30 minutes of silence, and kept rising. `plugins/_common/seam_denormals.h` (`Seam::ScopedNoDenormals`, FTZ and DAZ on x86, FZ on arm64, restored at the end of the scope) now wraps `Engine::process`; a test feeds 1e-310 and requires exact zeros and the caller's state restored (RED before the fix). After the fix: 9.4 % of a core at 96 kHz after 10 minutes of silence, as at the start. Every comparison with Faust is unchanged.
- Important, open until the host check: `uidesc_lint_selftest` and `make -C doc test` wait for `docs/img/stunedrev.png`.
- Minor, deferred: RESET's phases switch at block boundaries, so the moment the sound resumes moves by up to two blocks with the block size (the final state is identical); input and output are not clamped against a corrupt state blob; the off-grid test re-derives the SDK formula rather than calling `RangeParameter`; no test runs a section with no spare sample; the RESET view has no `onMouseCancel` (as dslar); a comment says 1.2 GiB where the measure is 1.15.

## Host check (Giuseppe, 2026-10-02)
Giuseppe loaded STUNEDREV in Reaper at 96 kHz and reported everything in order; the screenshot (`docs/img/stunedrev.png`) shows the window as designed, the bus 4 in + out, and the footer reading the centroids 106, 69, 17, 201 s and the arena, 588 MiB at 96.0 kHz, ready.
With the screenshot `make -C doc test` passes its 13 checks, `tools/check-uidesc.py` reports no warning, and ctest passes 37 of 37.
