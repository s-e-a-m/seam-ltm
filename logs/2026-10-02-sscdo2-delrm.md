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
