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

## `decay-from-glide.py` — reverberation time from an `ltglide` pass

Reads T60 out of a recorded `ltglide` pass, so a calibration can state where
its own measurement stops describing the loudspeaker and starts describing
the room.

```bash
.venv/bin/python tools/decay-from-glide.py REC.wav \
    --t 120 --f0 315 --f1 40 --delta 2.0 --volume 96
```

Requires `numpy`, `scipy` and `soundfile` (`requirements.txt`); the rest of
`tools/` is standard-library only.

`ltglide` emits Linkwitz bursts — five cycles under a Hann window — each
followed by `delta` seconds of silence, so every burst is its own
interrupted-excitation experiment in the sense of ISO 3382-2.
A pass therefore yields **T60(f)**, one estimate per burst, which is what
matters below the Schroeder frequency where decay is individual modes
ringing rather than a room average.

The burst onsets are **reconstructed, not detected**: the head Dirac fixes
the timeline and `GlideTransport` puts the glide exactly `kLeadSec` later, so
the schedule follows from `f0`, `f1`, `T`, `delta` and the two modes — the
same reasoning the `ltglide` receiver uses to regenerate its reference.
The measured span is checked against `kLeadSec + T + kTailSec` and a
mismatch is reported, because wrong parameters otherwise produce a
plausible-looking table.

The head Dirac's own 5 s of silence is an integrated-impulse-response
measurement — the ISO 3382 reference method — and is analysed as a second
opinion per octave band.
It is gated on both the noise floor and the standard's curvature criterion,
so it either agrees with the bursts or says nothing; one sample of impulse
carries little energy, and "nothing resolved" is the ordinary outcome.

Record **through the calibration preset**, at the level the calibration used.
T60 is a property of the room and does not depend on the source spectrum, but
the preset's electrical curve is the inverse of the loudspeaker's acoustic
one, so with it in circuit every burst arrives at the microphone at the same
SPL and one `ltglide` level clears the noise floor across the whole band.
Bypassing it restores the raw response, and reaching the weakest band then
means driving the strongest one tens of dB harder.

Validated against synthetic passes convolved with a known exponentially
decaying impulse response: true 0.8 / 1.5 / 2.5 s read back as
0.86 / 1.60 / 2.44 s, so the Schroeder frequency it derives is good to a few
percent.
With the noise floor raised to −35 dBFS it declines to estimate at all
rather than returning the wrong number.

## `bench-pink.cpp` — the pinking filter's CPU cost, and the pole density it settles

Answers one question: does `multipink`'s 64-stream pinking filter cascade
(`plugins/multipink/source/multipink_pink.h`) cost an amount of CPU that
justifies its accuracy, and would a denser pole ladder (`kPolesPerOctave`
1.5 instead of 1.0) still be affordable? It is not wired into the build —
CPU cost is a design decision made once, not a regression a ctest should
gate.

```bash
cd tools && clang++ -O3 -std=c++17 -I../plugins/multipink/source \
    -o bench-pink bench-pink.cpp && ./bench-pink
```

It measures two loop orders at two sample rates (48 kHz, 192 kHz), 7 repeats
each, and reports min/mean/max so the numbers describe a spread, not one
lucky run.
Order A is the production loop order in
`plugins/multipink/source/multipink_processor.cpp` (section-outer,
channel-middle, sample-inner): correct, but 16-18 full passes over the
128-256 KB scratch buffer per block, which does not fit L1/L2.
Order B interchanges it (channel-outer, sample-middle, section-inner),
keeping one channel's row resident in L1 and carrying only the ≤18 filter
state scalars per channel through the inner loop; it is roughly 2x faster
in every cell measured, and is a candidate for a later change to the
production loop — this tool only measures, it does not touch
`multipink_processor.cpp`.

Density is read from whatever `kPolesPerOctave` the included
`multipink_pink.h` currently has, so comparing densities means editing the
header and rebuilding.
On the Intel Core i7-8850H (x86_64) this was measured on, 1.5 poles/octave
at 192 kHz costs 72-129% of one core depending on loop order — far past the
5% ceiling that would justify it — so the suite ships `kPolesPerOctave =
1.0`, which costs 8-88% of one core across the same cells and meets the
SMPTE ST 2095-1 tolerance with 0.07-0.08 dB of margin against 0.25 dB.
Full numbers, both loop orders, both rates, both densities:
`.superpowers/sdd/2026-08-19-pink-filter-mz/task-6-report.md`.
