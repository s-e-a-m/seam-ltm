# tools

Repository-level tooling.
Nothing here is required to build the suite; each script is a check or a
generator run on demand.

## `check-uidesc.py` — UI standard lint

Checks every `plugins/*/resource/*.uidesc` against `doc/style/ui-style.md`:
XML validity first, then title, palette, absence of `TextDim`, explicit
white `font-color` on text, and the vertical order of SETUP, OPS and FINE.
Four further rules leave the XML for `plugins/*/source/`, each plugin's
`CMakeLists.txt`, and the README gallery.
One scans every `.h` and `.cpp` for the name `TextDim`, because the five
plugins that draw text from custom views name their colours in C++ where no
`.uidesc` rule can see them.
The second reads the display name each `*_processor.cpp` registers with the
VST3 factory, in its `DEF_CLASS2` block, and requires the same
`SEAM <NAME>` the window title carries: the host draws that string in its
title bar and its plugin browser, so a plugin has two names, and until this
rule existed only the one inside the window was ever checked.
It parses the macro's argument list, so both `DEF_CLASS2` formattings in the
suite read alike; it checks the audio effect registration and skips a
processor file that registers no class.
The third reads two more names: `CMakeLists.txt`'s
`DESCRIPTION "SEAM <NAME> – ..."`, which names the CMake target for build
tooling and packagers, and `version.h`'s
`#define stringFileDescription "SEAM <NAME> – ..."`, the file-version
resource a file manager reads for "Get Info" or the Details tab — checking
every occurrence, since some plugins `#define` it twice, once inside a
64-bit conditional.
A plugin's name is written out by hand in four files (window, host title
bar, CMake description, file-version string); this rule and the factory-name
rule together are what closed the gap left by checking only the first of
them, which is how a stale name survived unnoticed in the other three for a
full development cycle.
Only the name is checked, never the subtitle after the dash, since the
subtitle is prose allowed to differ between files; a file with no
`SEAM <NAME>` prefix, or that does not exist, is skipped rather than flagged.
The fourth reads `README.md` and `docs/img/`: every plugin that ships a GUI
must be photographed at `docs/img/<name>.png` and shown in the README
gallery.
Both misses are warnings, not errors — a missing screenshot embarrasses the
documentation, it does not ship a broken plugin — and the check exists
because the gallery drifted by hand three times, leaving eight windows
un-shown until they were added back one afternoon.
HEADER and FOOTER placement is not checked; see doc/style/ui-style.md
("Running the lint") for the exact comparisons and how each zone is
recognised.

```bash
python3 tools/check-uidesc.py                 # every plugin
python3 tools/check-uidesc.py path/to.uidesc  # one file
```

All four whole-suite scans belong to the whole-suite run: naming one
`.uidesc` asks about that document alone.
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
