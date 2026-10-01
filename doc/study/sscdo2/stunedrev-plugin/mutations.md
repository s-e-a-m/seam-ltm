# stunedrev plugin — mutation record

Every test of the stunedrev plugin and of the two libraries it added to `_common/` was broken on purpose to show that it sees the defect it is there for.
Each mutation was applied to the source by `mutate.py` (this folder), the test rebuilt and run, and the source restored with `git checkout`.
Three mutants survived the first run; each was a hole in a test, which was fixed and the mutant run again.

| test | mutation | result |
|---|---|---|
| seam_primes_test | nextPrimeAbove starts at n when n is odd (not strictly greater) | RED |
| seam_primes_test | sieve inner step p instead of 2p | RED |
| stunedrev_dsp_test | floor(ms*fs/1000) without +0.5 (truncation, Davide's rule) | RED |
| stunedrev_dsp_test | phi written 1.618 | RED |
| seam_moorer_test | +g instead of -g | RED |
| seam_moorer_test | read t back instead of t-1 | RED |
| seam_moorer_test | clear() keeps v | RED after the fix (was GREEN: an impulse through t = 13 writes only every 13th cell, and at the clear v happened to be 0; the test now drives the section with a dense input) |
| stunedrev_dsp_test | sectionLength without +1 | RED |
| stunedrev_dsp_test | ratios e and pi swapped | RED |
| stunedrev_dsp_test | the loop over sections stops at 41 | RED |
| stunedrev_dsp_test | setTime stores the time, applies it only in prepare | RED |
| stunedrev_dsp_test | outputs zeroed at the start of the block (in-place hazard) | RED |
| stunedrev_dsp_test | setGain(kG) omitted in prepare (g = 0) | RED |
| stunedrev_dsp_test | attach() does not clear the state (prepare at a new rate) | RED |
| stunedrev_dsp_test | centroid divided by 96000 instead of fs | RED |
| stunedrev_dsp_test | setPower ignored | RED |
| stunedrev_dsp_test | RESET: a chunk skipped | RED |
| stunedrev_dsp_test | RESET: sections not cleared at the end | RED |
| stunedrev_dsp_test | RESET: a click during the clearing is deferred | RED after the fix (was GREEN: the rest of the first clearing, 0.31 s, still passed a 0.3 s lower bound; the test now measures one whole RESET on a fresh engine and requires the second click to take exactly that) |
| stunedrev_dsp_test | RESET: output buffer not zeroed during the clearing | RED |
| stunedrev_dsp_test | RESET: a click before prepare is replayed | RED after the fix (was GREEN: the check came after one second, when the replayed RESET had already finished; it now comes after the first block) |
| stunedrev_params_test | lround(norm*99) instead of the SDK's int(norm*100) | RED |
| stunedrev_state_test | readState does not store what it read | RED |

The in-place mutation zeroes the outputs at the start of the block, the pattern a refactor would most likely introduce: each line reads its own channel before writing it, so no other ordering can break the in-place case.
