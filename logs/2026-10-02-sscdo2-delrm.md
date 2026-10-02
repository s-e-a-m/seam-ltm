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

The plan's ten tasks, as done on the branch `delrm-plugin`, each with its tests verified by mutation:

1. Session log and the metres rule in `seam_primes.h` (`metresToPrimeSamples`, DDELAY's rounding and 331.4 m/s).
2. `seam_delays.h`, `Seam::IntegerDelay` equal to `de.delay`.
3. `seam_filters.h`, `Seam::LeakyIntegrator` equal to `sfi.leakyint(1)`.
4. `seam_compressors.h`, `Seam::CompressorMono` equal to `co.compressor_mono`, `gainDb()` for the meter.
5. The engine `delrm_dsp.h`, four channels, equal to `sdt.delrm*` at 96 and 48 kHz and across a change of distance, with block meters.
6. `ParamBox` of atomics and the append-only state.
7. The processor, the footer with D, the S window with the four input meters and the two GR meters.
8. The mutation record and the CPU measurement.
9. This documentation: plugin README, study README, registry (nineteen plugins), log, report.

### Deviations found during implementation

- The tolerance of the 48/96 kHz sine-gain test rose to 3e-3: at 48 kHz the sampling of the peak limits the precision.
- `tests/ref_windows.h` gained guards for NaN and for the count of compared samples after review: a comparison of nothing, or of NaN, no longer passes.
- The "updated envelope" mutant of the compressor was found equivalent (a > (1−c)a + c·env is a > env for c > 0) and replaced by "signed x", which the test sees.
- The state test pins the on-disk order of the parameters (Power, Distance, Output).
- The millimetre rounding is pinned by 7.0372 m: 2039 samples rounded, 2053 unrounded, at 96 kHz.

### Verification numbers

- `LeakyIntegrator` against `sfi.leakyint(1)`: relative error 0, bit-exact, at 96 and 48 kHz.
- `CompressorMono`: output 3.6e-16 (11:1) and 2.6e-16 (4:1); gain 1.1e-14 dB and 3.6e-15 dB.
- Engine against the specification: 5.28e-16 (96 kHz), 5.98e-16 (48 kHz), 5.34e-16 across a change of distance at sample 96000.
- Line memory at 384 kHz: 1.06 MiB (4 × 34 764 doubles); the test reports nothing larger.

### Mutations and CPU

24 mutations (`doc/study/sscdo2/delrm-plugin/mutations.md`): 22 RED, 2 GREEN.
The two GREEN are equivalent mutants: the updated-envelope switch (equal for c > 0) and the removal of `ScopedNoDenormals` (equal for the output, caught by the CPU measurement instead).
Run on Debug; the two `seam_delays_test` rows were re-confirmed RED in Release, since a Debug build aborts that test on its own clamp assert.
CPU, 256-sample blocks: 96 kHz 2.02 % of a core on sound and 0.46 % on silence; 192 kHz 4.12 % and 0.91 %; without `ScopedNoDenormals` the silence costs 2.70 % and 5.45 %, about six times more.

## Open

- The host check in Reaper (Giuseppe): 96 kHz, `ccb_dry.wav` through the four channels, by ear against the `delrm-comb` and `delrm-rm` renders; the distance by hand; GR 2 and GR 4 against the inputs; the same session at 48 kHz; the screenshot for `docs/img/delrm.png`.
- The dcblocker listening: whether the thinner bass wants the two declared `fi.dcblockerat(76.59)` back.
- The 6 deferred minors of stunedrev, if still open.
- The choir's survey.
