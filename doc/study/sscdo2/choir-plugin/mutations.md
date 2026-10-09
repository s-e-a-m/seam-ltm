# choir plugin — mutation record

Every test of the choir plugin and of the three libraries it added to `_common/` (basics, svf, analyzers) was broken on purpose to show that it sees the defect it is there for.
Each mutation was applied to the source one at a time with `sed`, only the named test target was rebuilt and run, and the source was restored from a copy (`cmp` equal before the next).

| test | mutation | result |
|---|---|---|
| seam_basics_test | time constant doubled in tau2pole | RED |
| seam_basics_test | epsilon test `< 0.0` instead of `< DBL_EPSILON` | RED |
| seam_svf_test | v1 multiplied by the denominator instead of divided | RED (both impulses, peak gain) |
| seam_svf_test | k = q instead of 1/q | RED (both impulses, peak gain) |
| seam_svf_test | the low-pass output v2 returned | RED on both impulses; the peak-gain case stays green: the SVF low-pass is also q at f |
| seam_analyzers_test | smoother without the max (no immediate attack) | RED (reference, attack) |
| seam_analyzers_test | release halved | RED (reference, release) |
| choir_dsp_test | channel c sings on channel c+1's streams | RED (96 and 48 kHz) |
| choir_dsp_test | choirdens missing | RED at 48 kHz only (1 at 96 kHz) |
| choir_dsp_test | choirdens applied twice | RED at 48 kHz only |
| choir_dsp_test | the stretch a on the analysis bands | RED (96 and 48 kHz) |
| choir_dsp_test | follower release halved | RED (96 and 48 kHz) |
| choir_dsp_test | inactive bands computed, designed at their own centre | RED (non-finite: unstable above fs/2) |
| choir_dsp_test | RESET clears the bands but keeps the followers | RED (RESET test) |
| choir_dsp_test | display without the division by Q | RED (display test) |
| choir_dsp_test | RESET also rewinds the noise (`noise_.reset()` in the reset branch) | RED ("RESET does not rewind the noise", added after the final review) |
| choir_dsp_test | ScopedNoDenormals removed | RED (subnormal test) |
| choir_params_test | Power and Output read from each other's slot in plain() | RED |
| choir_state_test | missing fields filled with 0.9 instead of the defaults | RED (short blob) |

## Notes
The two mutations on `choirdens` are RED at 48 kHz only: at 96 kHz the factor is 1, so a test run at that rate alone would let both through.
The low-pass mutation of `SvfBandpass` passes the peak-gain case, because Simper's low-pass also has gain q at the resonance; the impulse responses catch it.
An inactive band skipped but designed at its own centre is not a defect anyone can hear (it is never computed); the mutation removes both the skip and the 19999 Hz design, and the output goes non-finite above fs/2.

## After the rehearsal with STONED (2026-10-09): 1/f, Q, release

| test | mutation | result |
|---|---|---|
| choir_dsp_test | make-up fixed at 1 | RED (white-noise power; sdt.choirw reference) |
| choir_dsp_test | make-up summed on fl alone | RED (white-noise power; sdt.choirw reference) |
| choir_dsp_test | weight sqrt(f/fv) instead of f/fv | RED (20log10(k) test) |
| choir_dsp_test | the crossfade ignored (`0 * pm`) | RED (20log10(k) test) |
| choir_dsp_test | 1/f snaps instead of crossfading | RED (25 ms crossfade) |
| choir_dsp_test | setPink(false) ignored once on | RED (off is the score's choir) |
| choir_dsp_test | applyQ leaves Q*2*pi stale | RED (new Q equals the choir designed at it) |
| choir_dsp_test | the Q grid restarted every block | RED (block size does not move the grid) |
| choir_dsp_test | setRelease leaves the followers' pole | RED (new release equals the choir designed with it) |
| choir_params_test | applyTo without setQ / without setPink | RED (applyTo) |
| choir_params_test, choir_state_test | release default 0 instead of 1.5 s | RED (defaults; old session recall) |

A mutation that survived is recorded because it taught something: `pinkW - 0.9` instead of `pinkW - 1` is green, and rightly so — with the crossfade at 0 the weight is multiplied by zero, so the mutant is the same program.
The test that guards "off is the score's choir" is killed instead by a switch that cannot return to off.
