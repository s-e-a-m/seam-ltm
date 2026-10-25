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

## Repository structure

- [ ] ([#11](https://github.com/s-e-a-m/seam-ltm/issues/11)) The repository has both `doc/` (style, study, the plugin registry) and `docs/` (screenshots, audio, superpowers). The workspace convention keeps one `docs/` for documentation source and `refs/` for reference material. Decide the final layout and move things once, rather than adding to both.

## Studies

- [ ] ([#12](https://github.com/s-e-a-m/seam-ltm/issues/12)) Quadrature pair: the same RBJ all-pass sections realised in direct form (`fi.tf2`, as `seam.filters.lib` does now) and as SVFs (`fi.svf.ap`), after GRAME moved `filters.lib` to SVF sections for float accuracy (#261, #262). Phase error in float and double from 44.1 to 192 kHz, CPU. A necessary step before proposing SEAM work to GRAME.
