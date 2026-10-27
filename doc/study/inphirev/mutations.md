# inphirev — mutations

| Test | Mutation | Result |
|------|----------|--------|
| `seam_basics_test` speedOfSound | `273.15` → `273.0` in `Seam::speedOfSound` | RED (room-temperature check fails), restored |
| `inphirev_dsp_test` delay table | drop the `+ 0.5` in `sectionDelay` (floor instead of round) | RED (delay table, 4 rates; ids 1 and 2 share a delay), restored |
| `inphirev_dsp_test` delay table | prime `>=` n instead of strictly above (`nextPrimeAbove(n - 1)`) | RED (delay table, 4 rates; ids 1 and 2 share a delay), restored |
| `inphirev_dsp_test` ids 1 and 2 | `gridPhase(2)` returns 0 | RED (900 shared-delay failures, plus the delay table), restored |
