# ALOGOS mutations

Every formula of `alogos_dsp.h` was broken once, the test run was seen RED, and the formula was restored.

## Task 4: scales, grid, delay table (`alogos_dsp_test`)

| Formula | Mutation | Tests that failed |
|---|---|---|
| grid | exponent denominator `4·D` became `4·D + 1` | default setting is INPHIREV; delay table equals sgs.aldel; snap is the nearest grid point; ids 1 and 2 never share a delay |
| delay | the delay divided by `c` and multiplied by 1.001 | default setting is INPHIREV; delay table equals sgs.aldel; faces increasing |
| snap | `- gridPhase(id)` became `+ gridPhase(id)` | snap is the nearest grid point |
| N_max | `(n + 3/4)` became `(n + 1/4)` | N_max is a time; delay table equals sgs.aldel; faces increasing |
| phase | id 2 phase 0.5 became 0 | contacts between ids 1-4 are short; default setting is INPHIREV; delay table equals sgs.aldel; ids 1 and 2 never share a delay |
| J (gridMax) | last-STONE phase 7/8 became 1/2 | delay table equals sgs.aldel; grid range |
