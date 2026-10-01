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
- stunedrev.

## Host check, listening and registry
Giuseppe listened in Reaper at 96 kHz (POWER and volume fades, cue 1, cue 2 with glide 120 then f 112.67, a Δ sweep, the same session at 48 kHz): listening OK.
Giuseppe loaded LMO in Reaper and sent the window's screenshot (`docs/img/lmo.png`): the window is as designed, the bus is 4 in + out, and the read-only "f now" reaches the footer (440.00 with f at 440), which the final reviewer had left to the host check.
The registry gains a family for the works, "Works — SSCDO#2", with LMO; the counts of `doc/scripts/test-doc.sh`, `render-readme.py` and `CLAUDE.md` go from sixteen to seventeen; `make -C doc test` passes its 12 checks, `uidesc_lint_selftest` passes with the screenshot.
- Then stunedrev.
