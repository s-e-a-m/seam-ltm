# tools

Repository-level tooling. Nothing here is required to build the suite;
each script is a check or a generator run on demand.

## `check-uidesc.py` — UI standard lint

Checks every `plugins/*/resource/*.uidesc` against `doc/style/ui-style.md`:
XML validity first, then title, palette, absence of `TextDim`, explicit
white `font-color` on text, and zone order.

```bash
python3 tools/check-uidesc.py                 # every plugin
python3 tools/check-uidesc.py path/to.uidesc  # one file
```

Errors exit non-zero; zone-order findings are warnings and do not.
Standard library only — no virtualenv, no pip step.

Wired into the build as two ctests:

```bash
ctest --test-dir build -R uidesc
```

`uidesc_lint_selftest` runs `test_check_uidesc.py`, which proves each rule
against inline fixture XML; `uidesc_lint` runs the lint over the real
plugins. The self-test exists because a lint that is only ever run against
files it already passes is a lint nobody has tested.

XML validity leads for a reason: no part of the build parses a `.uidesc`.
On 2026-07-21 a `--` inside a comment produced an empty editor in the host
while the compiler and the VST3 validator both reported success.

## `gen-faust-doc.sh` — Faust reference documentation

Regenerates, per plugin, the `-svg/` block diagrams and the mathdoc PDF
from the `.dsp` files that point at the SEAM Faust libraries.

```bash
tools/gen-faust-doc.sh
```

Requires `faust`, `faust2mathdoc`, `svg2pdf`, and `pdflatex` (with the
`breqn` package) on the PATH. The output is committed documentation, so
run it when the Faust specification changes, not on every build.
