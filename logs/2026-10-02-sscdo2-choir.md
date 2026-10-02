# 2026-10-02 — SSCDO#2: the choir plugin

Fourth C++ port of SSCDO#2, after LMO, stunedrev and delRM (`logs/2026-10-01-sscdo2-plugins.md`, `logs/2026-10-02-sscdo2-delrm.md`).
The choir's survey (noise, chain, specification `sdt.choir`) is in `logs/2026-09-29-sscdo2-ricognizione.md`, "The choir".
Spec: `docs/superpowers/specs/2026-10-02-choir-plugin-design.md`; plan: `docs/superpowers/plans/2026-10-02-choir-plugin.md`.

**Who:** Claude (agent), on Giuseppe's instructions.

## Decisions (Giuseppe, in the design)

- f, a, Q and the release are the performance's constants, shown in the window, not parameters.
- A 4×16 grid of the analysis bands, fed by 64 independent relaxed atomics (no invariant across bands asks for a snapshot).
- RESET from the GUI only, a generation counter served at the next block (stunedrev's pattern).
- New `_common` libraries mirroring standard Faust: `seam_svf.h` (`fi.svf.bp`), `seam_analyzers.h` (`an.amp_follower`), `seam_basics.h` (`ba.tau2pole`, moved out of `seam_compressors.h`). The 20 kHz rule stays in the choir's wiring.
- Window L (460 px) for the grid's width; POWER and RESET in OPS, output full width in FINE, the grid in the FOOTER; the bars in `MeterFill`.

## Work (branch `choir-plugin`)

- Task 1, `seam_basics.h`: `tau2pole` moved; compressor and delRM tests green.
- Task 2, `seam_svf.h`: equal to `fi.svf.bp` within 1e-13 (48 Hz Q 350 at 96 kHz; 1 kHz Q 0.7 at 48 kHz).
- Task 3, `seam_analyzers.h`: equal to `an.amp_follower(1.5)` within 1e-12.
- Task 4, the engine: equal to `sdt.choir(350, 1.5)` within 1e-12 of the peak at 96 and 48 kHz; block size 1, 512, 4096 identical; float bus within 1e-5; unprepared engine silent; RESET exact, also when requested with the host stopped; bands from 20 kHz inactive and the engine finite; no subnormal state after 60 s of silence; ramps of 25 ms at every rate; display within 0.1 dB; a new rate equal to a fresh engine.
- Task 5, parameters and state: POWER and output, append-only, two doubles; RESET not in the state.
- Task 6, the plugin: FUID 0x5E4D0013, 20th plugin; VST3 validator 47/47; uidesc lint 0 errors.
- Task 7: CPU 4.30 % of a core at 96 kHz for the engine (2.15 % at 48 kHz); mutation record; study `doc/study/sscdo2/choir-plugin/`; plugin README; report card `coro-uscita` (DA PROVARE).

Every test verified by mutation (`doc/study/sscdo2/choir-plugin/mutations.md`): 19 mutations, all RED; the two on `choirdens` only at 48 kHz.
Final review (fresh reviewer, whole branch): no Critical, no Important; one gap raised to Important and fixed, the spec's "the noise is not rewound" had no test (now `RESET does not rewind the noise`, RED when the reset rewinds it). Deferred minors: the grid keeps the last bars after deactivation; RESET gives no visual feedback while the host is stopped; one active flag for both centres (the grid would grey a band whose voice alone is at 20 kHz); the 20 kHz rule keeps bands stable only for fs >= 40 kHz (never reached with the constants).
The whole suite passes but `uidesc_lint_selftest`, which waits for `docs/img/choir.png`.

## Deferred to the end (with Giuseppe)

- The Release build of LMO and the choir together; the host check in Reaper at 96 kHz (TETRAREC A into the four channels, the grid while the clarinet plays, RESET, POWER, output on CC86) and at 48 kHz; the screenshot `docs/img/choir.png`.
- The registry (`doc/plugins.toml`, twenty plugins) and the counts in `doc/scripts/test-doc.sh`, `render-readme.py`, `CLAUDE.md`; `make -C doc publish` after Giuseppe's confirmation.
- Until the Release build, the `choir.vst3` symlink in `~/Library/Audio/Plug-Ins/VST3` points to the Debug build of `build/`.

## Host check and registry (Task 8, with Giuseppe)

- Release build of LMO and the choir together (19:57): both validators 47/47, minos 11.0; the `~/Library/Audio/Plug-Ins/VST3` links point to `build/VST3/Release`. LMO now carries the noise in three blocks.
- Giuseppe in Reaper: everything works at 96 and at 48 kHz; CPU and memory very contained. The Release window shows no live-editor tab.
- Screenshot `docs/img/choir.png`; registry entry CHOIR (twenty plugins), counts in `doc/scripts/test-doc.sh`, `render-readme.py`, `CLAUDE.md`; `make -C doc test` 13/13; uidesc lint 0/0; ctest 50/50.
- `make -C doc publish` waits for Giuseppe's confirmation.

## Release for Davide: `sscdo2-v0.4.0`

Giuseppe asked for a package of the SSCDO#2 plugins alone, for Davide to download (2026-10-02).
- Suite version 0.3.1 → 0.4.0 (four new plugins); the tag `sscdo2-v0.4.0`, not marked Latest, so v0.3.1 stays the release of the whole suite.
- Contents: LMO, STUNEDREV, DELRM, CHOIR, each `<name>-0.4.0-macOS.vst3.zip`, and `seam-ltm-sscdo2-0.4.0-macOS.zip` with the four. Nothing else: the report has no choir yet (Giuseppe); the quarantine command is in the release notes. No `libseamcalbus.dylib`: none of the four uses the bus.
- Full Release build, `ctest -C Release` 50/50 (minos_lint included); validator 47/47 on each; on the re-extracted bundles: version 0.4.0, `x86_64 arm64`, minos 11.0, `codesign --verify --deep --strict` ok.
