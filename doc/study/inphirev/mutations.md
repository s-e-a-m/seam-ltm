# inphirev — mutations

| Test | Mutation | Result |
|------|----------|--------|
| `seam_basics_test` speedOfSound | `273.15` → `273.0` in `Seam::speedOfSound` | RED (room-temperature check fails), restored |
| `inphirev_dsp_test` delay table | drop the `+ 0.5` in `sectionDelay` (floor instead of round) | RED (delay table, 4 rates; ids 1 and 2 share a delay), restored |
| `inphirev_dsp_test` delay table | prime `>=` n instead of strictly above (`nextPrimeAbove(n - 1)`) | RED (delay table, 4 rates; ids 1 and 2 share a delay), restored |
| `inphirev_dsp_test` ids 1 and 2 | `gridPhase(2)` returns 0 | RED (900 shared-delay failures, plus the delay table), restored |
| `inphirev_dsp_test` engine equals sgs.inphirev | last section of each face skipped (`i < N - 1`) | RED (windows and energy, all 6 rows), restored |
| `inphirev_dsp_test` change applies after 250 ms | a second change does not restart the pause | RED (applied before 0.35 s, 2 sequences, final state differs from a fresh engine), restored |
| `inphirev_dsp_test` setting equal to the applied one | trigger without `pending != applied` | RED (`clears() == 0` fails), restored |
| `inphirev_dsp_test` arena holds every delay | `sectionLength` over ids 0...4 instead of 0...8 | RED (`bad == 0`, every rate), restored |
| `inphirev_dsp_test` RESET restores a fresh engine | first chunk of the clear skipped | RED (windows and energy), restored |
| `inphirev_dsp_test` RESET click during the clearing | `clearPos_ = 0` removed at the start of the clear | RED (windows and energy differ from a fresh engine), restored |
| `inphirev_dsp_test` setting during the clearing | table applied from `applied_` instead of the snapshot | RED (the new setting never applies, `n < 5000000` fails), restored |
| `inphirev_dsp_test` readouts equal the table | `metres_` published for `j + 1` | RED (`metres() == d`, 4 rows), restored |
| `inphirev_dsp_test` prepare at a new rate | `fs_ = fs` removed from `prepare` | RED (delays of the old rate; first version of the test passed by coincidence because 96 kHz is the default, so the 96 -> 48 leg was added), restored |
| `inphirev_dsp_test` allocation failure | byte limit ignored | RED (`prepare` returns true, `failed()` false), restored |
| `inphirev_dsp_test` hours of silence | `ScopedNoDenormals` removed | RED (`subnormal == 0` fails), restored |
| `inphirev_decay_test` decay estimate | drop the backward integration (curve used as is) | RED (closed form, design figures, estimate vs engine: t30 = 0 s on every face), restored |
| `inphirev_decay_test` decay estimate | g instead of g^2 as the energy gain | RED (closed form, design figures, estimate vs engine: errors 80 %), restored |
| `inphirev_decay_test` beyond the horizon | threshold against the truncated (clipped) total instead of 1 | RED (single section at 500 s and 300 s: t30/t60 finite instead of +inf, 3 checks), restored |
| `inphirev_params_test` clamps | upper clamp removed from the stepped toPlain (`min(steps, ...)`) | RED (3 cases, 8 assertions: round-trip, off-grid, id 9 / j 20 / sections 40), restored |
| `inphirev_state_test` short read | a short blob keeps the box's previous values instead of the defaults (re-done after the fix round, no version field) | RED (short-blob case, 1 assertion), restored |
