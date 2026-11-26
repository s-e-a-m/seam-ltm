# TODO

## Bugs

- [ ] ([#13](https://github.com/s-e-a-m/seam-ltm/issues/13)) `multipink` and `ltburst` let the input through. Nothing inside the plugins can do it; likely the host passing the track channels the plugin does not own (`ltburst` is mono out). Reproduce with host and track width first.
- [ ] ([#14](https://github.com/s-e-a-m/seam-ltm/issues/14)) `addelay` unusable in concert: the air filter costs about 1 dB at 8 kHz at 10 m, but Spreading On applies `1/r` against 1 m, −20 dB. Spreading relative to the speaker's real distance.
- [ ] ([#15](https://github.com/s-e-a-m/seam-ltm/issues/15)) Speed of sound: `isos = 331.4` is the 0 °C value, inherited by `ddelay`, `addelay`, `delrm`; `addelay` ignores its own Temperature for the delay. Fix `seam.math.lib` first. Found on dream-tetrarec.

## Enhancements

- [ ] ([#16](https://github.com/s-e-a-m/seam-ltm/issues/16)) `strx`: more detail in the grid. Decide which grid first.
- [ ] ([#17](https://github.com/s-e-a-m/seam-ltm/issues/17)) `multipink` with four stones in the corners: the instances are uncorrelated, so the sum is +6 dB flat; a low-frequency rise is the room. Measure one against four before compensating anything.
- [ ] ([#18](https://github.com/s-e-a-m/seam-ltm/issues/18)) `m2xhgr` stays Haar; the Linkwitz-Riley encoder in Hz becomes a separate plugin with its own name.
- [ ] ([#19](https://github.com/s-e-a-m/seam-ltm/issues/19)) `m2xhgr`: input and output meters.
- [ ] ([#20](https://github.com/s-e-a-m/seam-ltm/issues/20)) Draw the rotation vector in `m2xhgr`, `lr2xhgr`, `b2xrot`, `xyprrot`: one shared view in `_common/`.

## Documentation

- [ ] ([#9](https://github.com/s-e-a-m/seam-ltm/issues/9)) Fill in the Faust counterpart for the plugins that still have none in `doc/plugins.toml`: `B2XROT` and `XYPRROT` have no library-side match, and `DDELAY` only matches `seam.math.lib` weakly. Leave the field empty rather than guess.
- [ ] ([#10](https://github.com/s-e-a-m/seam-ltm/issues/10)) The links to the Faust libraries point at the sources on GitHub, not at the published reference: apart from `basic` and `math`, the library pages do not exist yet because those `.lib` files have no documented functions. Point them at the reference as it becomes available.

- [ ] `doc/scripts/test-doc.sh` checks the plugin count in the registry but not in the prose of the README and of `publish.py`: add a check, so the number cannot go stale again (2026-11-04).

- [x] `lessness`: checked in Reaper (Giuseppe and the agent, 2026-11-26): `resumes` in the sheet, NEW SHEET, FOLDER (fixed: VSTGUI handed AppKit a nil type list and no chooser appeared), the red mark on a force-detached disk image, the return with the 6 held lines moved to `sheets-held/`, `tools/hub-sheet.py --check` on every sheet; screenshot `docs/img/lessness.png` added.
- [ ] `lessness`, from the Reaper check: decide whether a new sheet re-declares the members present (a sheet opened by NEW SHEET names `strx#1` only at its first `resumes`), and whether the recent-lines pane starts empty on a new sheet (Giuseppe).
- [ ] `lessness`: a live session with no usable sheet path (every name taken, `HOME` unset) and held lines reads as "no session" with no red mark; `sheetAlarm` should rise on `pendingLines > 0` whatever the path (final review of plan 2, 2026-11-26).
- [ ] `lessness`, minor and deferred from plan 2's reviews: timers started in the constructors rather than in `attached()`; output channels beyond the input count not zeroed; no test for a null input channel; `SEAM_LESSNESS_PURE` split rather than a pure header; `tildePath` with `HOME` ending in `/`; the footer's dropped count is process-wide; lock-order comments in `seam_hub.cpp` omit `lifeMutex`.

## Repository structure

- [ ] ([#11](https://github.com/s-e-a-m/seam-ltm/issues/11)) The repository has both `doc/` (style, study, the plugin registry) and `docs/` (screenshots, audio, superpowers). The workspace convention keeps one `docs/` for documentation source and `refs/` for reference material. Decide the final layout and move things once, rather than adding to both.

## Studies

- [ ] ([#12](https://github.com/s-e-a-m/seam-ltm/issues/12)) Quadrature pair: the same RBJ all-pass sections realised in direct form (`fi.tf2`, as `seam.filters.lib` does now) and as SVFs (`fi.svf.ap`), after GRAME moved `filters.lib` to SVF sections for float accuracy (#261, #262). Phase error in float and double from 44.1 to 192 kHz, CPU. A necessary step before proposing SEAM work to GRAME.
