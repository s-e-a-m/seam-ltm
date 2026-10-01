# LMO plugin — mutation record

Every test of the LMO plugin was broken on purpose to show that it sees the defect it is there for.
Each mutation was applied to the source, the test rebuilt and run, and the source restored.

| test | mutation | result |
|---|---|---|
| seam_noise_test: bit for bit | streams in step order (`steps_[k]`) instead of reverse (`steps_[n-1-k]`) | RED |
| seam_noise_test: bit for bit | first step fed back (`state_ = steps_[0]`) instead of the last | RED |
