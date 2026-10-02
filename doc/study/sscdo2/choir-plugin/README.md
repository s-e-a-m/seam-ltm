# The choir C++ plugin

Part of the SSCDO#2 port, after `../choir-noise/` and `../choir-chain/`.
`plugins/choir` is the choir of *Studio sul Corpo d'Ombra #2* (`pitchDetectorChoirMcAdams`, four instances) written again by hand in C++ from `sdt.choir(350, 1.5)` of `seam.tedesco.lib`.
Spec: `docs/superpowers/specs/2026-10-02-choir-plugin-design.md`; plan: `docs/superpowers/plans/2026-10-02-choir-plugin.md`; log: `logs/2026-10-02-sscdo2-choir.md`.

## References
`gen-ref.sh` compiles the DSPs of `dsp/` with `faust -double` against the faustlibraries clone and renders them with LMO's `refdump.cpp` (`../lmo-plugin/`) into headers under `tests/ref/`; the tests read the committed headers and need no `faust` binary.

| DSP | header | what it fixes |
|---|---|---|
| `dsp/svf48.dsp`, `dsp/svf1k.dsp` | `seam_svf_ref.h` | `fi.svf.bp` impulse responses: 48 Hz, Q 350, 96 kHz; 1 kHz, Q 0.7, 48 kHz |
| `dsp/follow.dsp` | `seam_analyzers_ref.h` | `an.amp_follower(1.5)` on a 48 Hz sine switched off at 1 s, around the stop and a second later |
| `dsp/choir.dsp` | `choir_ref.h` | `sdt.choir(350, 1.5)` on 16 sines at f·k per channel, 2048 samples after 3 s at 96 kHz, 1024 after 3 s at 48 kHz |

The inputs are generated inside the DSP (`sin(2π·f·n/SR)`) and again in the C++ tests by the same formula, so both sides hear the same numbers.

## Results
- `SvfBandpass` equals `fi.svf.bp` within 1e-13 of the peak; `AmpFollower` equals `an.amp_follower` within 1e-12.
- The engine equals `sdt.choir` within 1e-12 of the peak on the four channels at 96 and 48 kHz, its noise (block 3 of `multinoise(72)`) and `choirdens` included.
- The output does not depend on the host's block size (1, 512 and 4096 give identical samples); a float bus equals a double bus within 1e-5 of the peak.
- RESET silences the ringing bands exactly; a request made while the host is stopped is served at the next block.
- 60 s of silence leave no subnormal state.
- VST3 validator: 47 of 47.
- CPU (`cpu.cpp`, Apple Silicon, `-O3`, 256-sample blocks): 4.30 % of a core at 96 kHz and 2.15 % at 48 kHz for the engine alone (the silent run; the cost does not depend on the signal); 9.91 % and 4.98 % with the 64 sines of the bench's input synthesis.

Every test was verified by mutation: `mutations.md`.

## Files
| file | what it is |
|---|---|
| `gen-ref.sh` | renders the references into `tests/ref/` |
| `dsp/*.dsp` | the reference DSPs |
| `cpu.cpp` | the CPU bench (build line in its header) |
| `mutations.md` | the mutation record |
