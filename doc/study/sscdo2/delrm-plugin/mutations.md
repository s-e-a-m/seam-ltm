# delRM plugin — mutation record

Every test of the delRM plugin and of the four libraries it added to `_common/` (primes metres, delays, filters, compressors) was broken on purpose to show that it sees the defect it is there for.
Each mutation was applied to the source one at a time, only the named test target was rebuilt and run, and the source was restored with `git checkout` (`git diff` empty before the next).

| test | mutation | result |
|---|---|---|
| seam_primes_test: metres | `kSpeedOfSoundInterior = 343.0` | RED |
| seam_primes_test: metres | no millimetre rounding (`mm = mt`) | RED |
| seam_delays_test | `r = pos_ + len_ - d_ + 1` (one sample short) | RED |
| seam_delays_test | read before write (`d = 0` returns the oldest sample) | RED |
| seam_filters_test | the pole built with `fc * 2` | RED |
| seam_filters_test | `x * invFs_` replaced by `x` (a sum of samples) | RED |
| seam_compressors_test | the knee removed (`knee_ = g`) | RED |
| seam_compressors_test | the switch compares with the updated envelope: `a > ((1 - cAtt_) * a + cAtt_ * env_)` | GREEN, equivalent mutant (see below) |
| seam_compressors_test | the switch compares the signed x, not \|x\| (`x > env_`) | RED |
| seam_compressors_test | `slope_ = 1.0 / ratio` (sign lost) | RED |
| seam_compressors_test: generic | `cKnee_ = tau2pole(attack, fs)` (no /2) | RED |
| delrm_dsp_test: engine 96/48 | `kIntegratorScale = 48000.0` | RED |
| delrm_dsp_test: engine 96/48 | channels swapped (`(c & 1) == 1` for the comb) | RED |
| delrm_dsp_test: engine 96/48 | `kRmGain = 1.0` | RED |
| delrm_dsp_test: change | `setDistance` stores `metres_` but `applyDistance` runs only in `prepare` | RED |
| delrm_dsp_test: memory | `lineLength` without `+ 1` | RED |
| delrm_dsp_test: rate change | `prepare` does not call `li_[r].prepare` (the old pole and state survive) | RED |
| delrm_dsp_test: in-place | `peak[c]` read from `out[c][k]` after the write | RED |
| delrm_dsp_test: in-place (extra) | `peak[c]` never measured (line deleted) | RED |
| delrm_dsp_test: meters | the block depth taken from the last sample, not the deepest | RED |
| delrm_dsp_test: release | `decay = std::exp(-1.0 / (kMeterRelease * fs_))` (per sample, not per block) | RED |
| delrm_dsp_test: silence | `ScopedNoDenormals` removed | GREEN, equivalent for correctness (see below) |
| delrm_params_test | `normalizedToDistance` without the clamp | RED |
| delrm_state_test | `readState` does not store the fields it read | RED |

Notes.

The plan's release mutation reads "per block, not per sample"; in the code the decay is already per block (`exp(-n / (kMeterRelease * fs))`), so the mutation applied is the reverse, a per-sample decay applied once per block, which the 300 ms release test catches.
The plan's `peak[c]` mutation was applied as the addition of a second measurement from `out[c][k]` after the write; the "never measured" variant is listed beside it.

The updated-envelope switch is an equivalent mutant for c > 0.
With a = |x| and env' = (1 - c) a + c env, a > env' is a - (1 - c) a > c env, that is c a > c env, that is a > env.
Both forms choose the same coefficient whenever the attack coefficient is non-zero.
(At attack = 0 the coefficient cAtt_ is 0, the mutant compares a > a, always false, and would pick the release; the delRM spec uses attack = 0.03 s, so this edge is outside the library's use here.)

`ScopedNoDenormals` leaves every output value unchanged, because the flush only removes subnormals that are far below any audible or measurable level; the output tests cannot tell it from a build without it.
The CPU check covers it, `cpu.cpp` in this folder (256-sample blocks, the test signal then 60 s of silence, one core):

| build | 96 kHz sound / silence | 192 kHz sound / silence |
|---|---|---|
| with `ScopedNoDenormals` (shipped) | 2.02 % / 0.46 % | 4.12 % / 0.91 % |
| without (scratch copy of the header) | 2.04 % / 2.70 % | 4.03 % / 5.45 % |

Without the guard the silent tail costs about six times more than with it, the subnormals filling the integrators and compressors; with it silence is cheaper than sound.

The mutations were run on the Debug configuration of `build-test`; the two `seam_delays_test` rows were re-confirmed RED in Release, because a Debug build aborts `seam_delays_test` on the assert of its own clamp test.
