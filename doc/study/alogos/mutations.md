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

## Task 5: arena broker and engine (`alogos_engine_test`)

Each mutation was applied by a script, the test built and run, and the file restored with `git checkout`.

| Rule | Mutation | Tests that failed |
|---|---|---|
| broker hand-over | `take()` accepts any delivered arena, not only the one of the latest request | broker: allocateNow, request, take, retire, cancel, limit |
| superseded request, delivered | the worker empties the slot of a superseded arena without freeing it | broker; release or destroy while allocating; prepare at a new rate |
| superseded request, being zeroed | the worker abandons an allocation whose request changed without freeing it | broker; a second change while allocating; release or destroy while allocating; prepare at a new rate |
| g alone clears | `sameTable` ignores g | g alone clears |
| identical table | `sameTable` ignores the delays (compares N and g only) | a change applies after 250 ms, once, and ends as a fresh engine |
| settle | a new setting sets the settle to 0 instead of 250 ms | a change applies after 250 ms, once, and ends as a fresh engine |
| reallocation threshold | the "no less than half the arena" condition removed | a change applies after 250 ms…; release or destroy while allocating; prepare at a new rate; RESET; N above N_max survives a scale round trip |
| audio thread allocates | `delete new int(0)` in `process()` while waiting for an arena | 12 of 17 test cases (every case that drives the engine through an allocation), not the counter's self-test |

The first run of "superseded request, being zeroed" stayed GREEN.
The engine test sent the second request before the worker had started on the first, so the abandoning branch never ran.
The test now waits until the worker holds the first arena and has not delivered it (`liveArenas() == 2 && !delivered()`).
The broker test now requests 1 GiB and supersedes it while the worker zeroes it.
After both changes the mutation went RED.

The hand-over mutation is caught by the broker test only.
At the engine level a delivered arena that was not requested last cannot reach `take()`: the 250 ms settle lets the engine take the first arena before a second request exists.
