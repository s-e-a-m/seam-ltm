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

## alogos_decay_test (Task 6)

| Rule | Mutation | Tests that failed |
|---|---|---|
| g is a parameter | `estimateDecay` squares the constant 1/φ instead of its `g` | single section, every g of the menu (4 of 5 values); estimate vs engine, π, g = 1/π (all four faces) |
| echo ratio | the echo energies fall by `g` per echo instead of `g^2` | single section; design figures; beyond the horizon; estimate vs engine, φ and π |

The φ case of "estimate vs engine" stays GREEN under the first mutation, as it must: at g = 1/φ the constant and the parameter agree.
Only the π case, and the closed form over the menu, can see that `g` is used.

## alogos_params_test and alogos_state_test (Task 7)

| Rule | Mutation | Tests that failed |
|---|---|---|
| clamp to the range | `clamp01` drops its upper bound | params (out-of-range clamp, off-grid mapping); state (recalled out-of-range) |
| NaN falls back to the default | `normalizedToDistance` returns `kMinMetres` for NaN | params (NaN fallback); state (recalled NaN) |
| stepped mapping is the SDK's FromNormalized | `steppedFloor` rounds `norm * steps` instead of flooring `norm * (steps + 1)` | params only (off-grid mapping) |
| a short blob keeps the defaults | `readState` starts from the box's current values instead of the defaults | state only (short blob of 4 doubles) |

Each mutation was restored and both tests were GREEN again.
The stepped-mapping mutation keeps the state test GREEN because the state test stores exact steps, which both mappings read alike.

### alogos_decay_test, fix round 1

| Rule | Mutation | Tests that failed |
|---|---|---|
| small perturbation | `g2 = (g * 1.05)^2` in the estimator | estimate vs engine, φ and π (all 8 faces, tolerance now 5 %); also the closed form, design figures and beyond the horizon |

The engine tolerance was tightened from 15 % to 5 % (worst observed error 1.4 %).

## Footer and parameter display, alogos_engine_test (Task 8)

The footer view and the controller's `toString` both print through `alogos_text.h`, so the tests read the strings the window shows.

| Rule | Mutation | Tests that failed |
|---|---|---|
| the footer's longest delay is face D's last attached section | `readout` takes `delay(kFaces - 1, n - 2)` | footer: first and longest delay (13 assertions, all four settings and the written-out default) |
| the distance shows the snapped metres | `distanceText` prints the metres asked, clamped, not the grid point | parameter display (177 assertions) |
| a clamped count shows the effective count | `sectionsText` prints the count asked before "(max N)" | parameter display (194 assertions) |

Each mutation was restored and the tests were GREEN again.
The existing "readouts equal the table" test stays GREEN under the first mutation, as it must: it reads the engine, not the footer.

## The allocation counter in Release, alogos_engine_test (Task 9)

The full Release ctest failed the counter's self-test: the compiler elided the probe `new int(3); delete p;`, which C++14 allows when the pointer never escapes, so the counter saw nothing.
The probes now go through a `volatile` pointer, and the self-test passes in Debug and in Release.
The Task 5 mutation `delete new int(0)` in `process()` is elidable in the same way, so in Release it would not show.

| Rule | Mutation | Tests that failed (Release) |
|---|---|---|
| the audio thread does not allocate | `{ static int* volatile mut; mut = new int(0); delete mut; }` at the top of `process()` | 13 of 20 test cases, among them "the audio thread does not allocate" |

The mutation was restored and the tests were GREEN again.
