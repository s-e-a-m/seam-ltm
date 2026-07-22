# tools

Repository-level tooling.
Nothing here is required to build the suite; each script is a check or a
generator run on demand.

## `check-uidesc.py` — UI standard lint

Checks every `plugins/*/resource/*.uidesc` against `doc/style/ui-style.md`:
XML validity first, then title, palette, absence of `TextDim`, explicit
white `font-color` on text, and the vertical order of SETUP, OPS and FINE.
Two further rules leave the XML for `plugins/*/source/`.
One scans every `.h` and `.cpp` for the name `TextDim`, because the five
plugins that draw text from custom views name their colours in C++ where no
`.uidesc` rule can see them.
The other reads the display name each `*_processor.cpp` registers with the
VST3 factory, in its `DEF_CLASS2` block, and requires the same
`SEAM <NAME>` the window title carries: the host draws that string in its
title bar and its plugin browser, so a plugin has two names, and until this
rule existed only the one inside the window was ever checked.
It parses the macro's argument list, so both `DEF_CLASS2` formattings in the
suite read alike; it checks the audio effect registration and skips a
processor file that registers no class.
HEADER and FOOTER placement is not checked; see doc/style/ui-style.md
("Running the lint") for the exact comparisons and how each zone is
recognised.

```bash
python3 tools/check-uidesc.py                 # every plugin
python3 tools/check-uidesc.py path/to.uidesc  # one file
```

Both C++ scans belong to the whole-suite run: naming one `.uidesc` asks
about that document alone.
Errors exit non-zero; zone-order findings are warnings and do not.
Standard library only — no virtualenv, no pip step.

The summary line counts `.uidesc` documents, and its error and warning
totals count messages rather than distinct defects: one `font-color="TextDim"`
trips both the palette rule and the absence rule, so a single mistake can
be reported twice.

Wired into the build as two ctests:

```bash
ctest --test-dir build -C Release -R uidesc
```

The `-C Release` is required because the suite is generated with Xcode, a
multi-config generator: without a named configuration `ctest` finds no test
to run and exits 8.

`uidesc_lint_selftest` runs `test_check_uidesc.py`, which proves each rule
against inline fixture XML; `uidesc_lint` runs the lint over the real
plugins.
The self-test exists because a lint that is only ever run against files it
already passes is a lint nobody has tested.

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
`breqn` package) on the PATH.
The output is committed documentation, so run it when the Faust
specification changes, not on every build.
