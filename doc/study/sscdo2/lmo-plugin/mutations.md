# LMO plugin — mutation record

Every test of the LMO plugin was broken on purpose to show that it sees the defect it is there for.
Each mutation was applied to the source, the test rebuilt and run, and the source restored.

| test | mutation | result |
|---|---|---|
| seam_noise_test: bit for bit | streams in step order (`steps_[k]`) instead of reverse (`steps_[n-1-k]`) | RED |
| seam_noise_test: bit for bit | first step fed back (`state_ = steps_[0]`) instead of the last | RED |
| seam_butterworth_test: order 24 | sections in reverse order | GREEN — an equivalent mutant: LTI sections in series commute, so the order changes only the rounding (below 1e-12 of the peak); nothing to detect |
| seam_butterworth_test: order 24, −3 dB | damping index shifted by one section (`s` from 0) | RED |
| seam_butterworth_test: order 24 and 2 HP | HP output without the damping (`v0 - v1 - v2`) | RED |
| seam_ramp_test: exact length | samples truncated instead of rounded (`(long)(seconds*fs)`; 1102.5 at 44.1 kHz) | RED |
| seam_ramp_test: exact landing | last step adds the increment instead of landing on the target | RED |
