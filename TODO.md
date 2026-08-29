# TODO

## Documentation

- [ ] Fill in the Faust counterpart for the plugins that still have none in `doc/plugins.toml`: `B2XROT` and `XYPRROT` have no library-side match, and `DDELAY` only matches `seam.math.lib` weakly. Leave the field empty rather than guess.
- [ ] The links to the Faust libraries point at the sources on GitHub, not at the published reference: apart from `basic` and `math`, the library pages do not exist yet because those `.lib` files have no documented functions. Point them at the reference as it becomes available.

## Repository structure

- [ ] The repository has both `doc/` (style, study, the plugin registry) and `docs/` (screenshots, audio, superpowers). The workspace convention keeps one `docs/` for documentation source and `refs/` for reference material. Decide the final layout and move things once, rather than adding to both.
