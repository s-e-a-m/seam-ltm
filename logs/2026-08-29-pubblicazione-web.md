# Session log — 2026-08-29 — The plugin suite on the SEAM site

## Goal
Phase 3 of the publishing plan: present the sixteen plugins at `/seam-ltm/`, generated from a registry rather than written by hand.

## What was found
Unlike `faust-libraries` and `sean`, this repository had no registry.
The plugin tables in `README.md` were the only source: prose formatted for reading, not for extraction.
A parser built on that file would break the first time the tables were reformatted.

The repository also had no `TODO.md` and no `logs/`.

## Decisions
`doc/plugins.toml` is now the registry and the single source for two outputs: the README tables and the site page.
Writing both generators at once rather than one now and one later — with two sources the copies diverge within months, and the README had to be read line by line anyway to extract the data.

TOML rather than YAML, as the spec suggested: PyYAML is not in the system Python, and the repository's `.venv` carries numpy and scipy for analysis, not for documentation.
`tomllib` has been in the standard library since Python 3.11, and a documentation generator must run on a freshly cloned machine.

Build, SDK and installation stay in the README: they age with the code, and a stale copy on a website is worse than no copy.

## Actions
- Registry extracted from the README with a throwaway script, not transcribed by hand. The sixteen descriptions were then verified identical to the originals, up to normalising `&rarr;` to `→`.
- `doc/scripts/`: `registry.py`, `render-readme.py`, `publish.py`, `navblock.py`, `test-doc.sh`; `doc/Makefile` with `doc`, `publish`, `test`.
- On the site: the `ltm` collection, CSS for the screenshots, an entry on the hub page.
- Added `TODO.md` and this `logs/` directory.

## Verification
`make -C doc test`: 12 checks green, including that the README is in sync with the registry and that a second publish leaves a single navigation block.
Visual check with headless Chrome: sixteen cards, screenshots served, table of contents complete.

One fix came from looking at the page: the screenshot sat between the plugin name and its I/O line, pushing the qualifying information away from the title. Order is now name, I/O, description, screenshot.

## Open
- `B2XROT` and `XYPRROT` have no Faust counterpart; `DDELAY` matches only weakly. Left empty rather than guessed.
- The Faust links point at sources on GitHub, not at published references: apart from `basic` and `math` those pages do not exist yet.
- The repository has both `doc/` and `docs/`.

## Who
**Who:** Claude (agent), on Giuseppe's instructions.
