# X2UHJ — AmbiX → UHJ C-format encoder/decoder

**Date:** 2026-06-05
**Status:** Design approved, pending spec review
**Workstream:** A (ambisonic toolkit completion) — first concrete deliverable
**Touches:** WS-A (plugin), WS-C embryo (analysis harness), backbone (lib↔plugin co-evolution)

## 1. Purpose and context

`X2UHJ` converts a First-Order Ambisonics signal in **AmbiX** convention into
the **UHJ C-format** (`L, R, T, Q`). In the SEAM/E4L vocabulary this is the
"UHJ decoder": it *decodes Ambisonics for 2-channel listening*. In classical
Gerzon terminology the same operation is the UHJ *encode* (B-format → UHJ).
The plugin label and documentation must state this explicitly so students are
not confused: **input is AmbiX, output is UHJ C-format.**

- `L, R` — stereo-compatible pair (the listenable UHJ stereo)
- `T, Q` — additional C-format channels for surround reconstruction / later
  decode

This is a **pedagogical** plugin: it teaches reading DSP from C++ source and,
through its companion analysis harness, teaches how an empirically-tuned
filter choice can be validated and improved against an analytic design.

### Reference materials

Author publications and primary sources (to be archived under the plugin doc
folder — see §9):

- Mastrorillo, Silvi, Scagliola — *A Guide to the Implementation of Ambisonics
  Super Stereo*, IJCIE Vol:17 No:11, 2023. (Primary algorithmic reference;
  Gerzon's matrix in eqs. 1–4, empirical filter coefficients in Table I,
  quadrature-error measurement protocol in Table II.)
- Mastrorillo, Silvi, Scagliola — *Implementing UHJ stereo in the Envelop for
  Live suite*.
- Silvi et al. — *The Ambisonics C-Format for Super Stereo, an open-source
  decoder* (NYCEMF).
- Gerzon — *Ambisonics in Multichannel Broadcasting and Video*, JAES 1983.
- Gerzon — US Patent 4,095,049 (Non Rotational Symmetric Encoding / UHJ), 1977.
- Gerzon — *Pictures reproduction 2-channel*, 1978.
- AIA48 Matera 2022 (aia48_279); 2022-AIA-Mastrorillo-Silvi-Scagliola.

## 2. The UHJ matrix (canonical, FuMa convention)

Gerzon's UHJ encode, in FuMa convention (W already scaled by 1/√2):

```
Σ = 0.9396926·W + 0.1855740·X
Δ = j(−0.3420201·W + 0.5098604·X) + 0.6554516·Y
L = (Σ + Δ)/2
R = (Σ − Δ)/2
T = j(−0.1432·W + 0.6512·X) − 0.7071·Y
Q = 0.9772·Z
```

`j` is the +90° (quadrature / Hilbert) phase operator. Coefficients are the
established UHJ constants and are NOT a design degree of freedom; the only
design freedom (and the only real risk) is the realisation of `j`.

## 3. Resolved DSP structure — "filter bank, then static matrix"

Resolves **Criticality 2** (the two source `.dsp` files disagree; `x2uhj.dsp`
is incorrect because it leaves the real-path components unfiltered and applies
only one network of the pair). The correct structure uses a **matched
quadrature pair** `H_R` / `H_I` with `arg(H_I) − arg(H_R) ≈ −90°` across the
band, *both* allpass (unit magnitude). Every component passes through `H_R`,
except the `j(...)` terms which pass through `H_I`.

Per-component filtering, then a purely algebraic mix:

```
W → W_r = H_R(W),  W_i = H_I(W)
X → X_r = H_R(X),  X_i = H_I(X)
Y → Y_r = H_R(Y)
Z → Z_r = H_R(Z)

Σ = 0.9396926·W_r + 0.1855740·X_r
Δ = (−0.3420201·W_i + 0.5098604·X_i) + 0.6554516·Y_r
L = (Σ + Δ)/2
R = (Σ − Δ)/2
T = (−0.1432·W_i + 0.6512·X_i) − 0.7071·Y_r
Q = 0.9772·Z_r
```

- 6 allpass cascades total: `H_R` on {W, X, Y, Z} and `H_I` on {W, X}.
  `Y` and `Z` never receive `j`, so they need only `H_R`.
- Note `Q = 0.9772·Z_r`: even the plain Z→Q term passes through `H_R`, so Q
  stays phase-coherent with L, R, T. (Passing raw Z would break C-format
  recombination.)
- Each cascade is 3 second-order allpass sections (6th-order quadrature
  network), matching the source design order.

## 4. Quadrature filter design — analytic re-derivation (Approach B)

The plugin ships **only** the analytically-derived equiripple allpass pair.
The 2023 empirical coefficients do NOT enter the plugin; they live in the
analysis harness (§6) purely as a comparison/validation term.

Rationale (over inheriting the empirical coefficients):

- **Reproducible and bounded**: an equiripple (elliptic) allpass-pair design
  specifies band, order, and max phase ripple as inputs and yields pole
  positions in closed form, with a guaranteed equiripple error around 90°.
- **Sample-rate correct by construction** (resolves **Criticality 5**): pole
  positions are recomputed for the actual sample rate in `setupProcessing`,
  rather than carrying coefficients anchored to a single SR.
- **Topology ambiguity dissolves** (resolves **Criticality 3**): the design
  produces pole positions directly. We place exactly those poles using a
  numerically robust realisation (TPT/bilinear preferred near Nyquist). The
  "RBJ vs TPT cookbook" question disappears because we no longer map
  `(fc, Q)` through a cookbook — we instantiate computed poles.

### Design parameters (compile-time / design-time, not user-facing)

- Band: 20 Hz – 20 kHz (target; final edges set during design to balance order
  vs. ripple).
- Order: 3 sections per network (6th order) as the starting point; the harness
  reports achieved phase error so the order can be revisited with evidence.
- Realisation: second-order allpass in TPT form, instantiated from computed
  pole radius/angle.

The exact pole positions are derived and verified by the harness (§6), NOT
asserted here from memory.

## 5. Parameters

**None.** `X2UHJ` is a stateless matrix encoder. Bypass and level are the
host's responsibility (host bypass, channel fader). This keeps the plugin a
pure, inspectable transform — consistent with SDMX.

Documented future (NOT in v1): pre-encode yaw rotation (reuse
`rotateYPR` from `seam.ambisonics.lib`), W/XY balance control.

## 6. Analysis harness — embryo of the measurement host (WS-C, 1:10 scale)

A focused **offline** analysis tool (Python or Octave), NOT the full
VST-loading host. Scope strictly bounded; boundaries designed so it can grow
into the full IR host later.

Responsibilities:

1. **Derive** the equiripple allpass pair (pole positions) for a given band /
   order / sample rate. Emit the coefficients consumed by the plugin.
2. **Measure** quadrature error: drive 1/3-octave sinusoids 20 Hz–20 kHz,
   compute `Re² + Im²` (should be 1 when quadrature is exact) — the automated,
   scientific version of the paper's Table II.
3. **Compare** the 2023 empirical coefficients against the equiripple design:
   overlay phase-difference curves and quadrature-error curves; produce
   validation figures.

Outputs committed to the repo: generated coefficients + validation figures.

Per `CLAUDE.md`, this is an acceptable one-off sketch/measurement tool; it does
NOT generate production DSP code that lands in `plugins/*/source/`.

Location: `plugins/x2uhj/tools/` (kept with the plugin it validates;
revisit when WS-C proper begins).

## 7. Normalization (Criticality 4, explicit)

Input is AmbiX (ACN order, SN3D normalization):

```
ACN index:  0=W  1=Y  2=Z  3=X
```

Convert to Gerzon's FuMa WXYZ convention before the matrix:

- reorder ACN [W, Y, Z, X] → [W, X, Y, Z]
- scale W by 1/√2 (SN3D → FuMa W)

Implemented in a single dedicated, commented function (e.g. `ambixToFuMa`).

## 8. Plugin file layout

Canonical suite layout:

```
plugins/x2uhj/
├── CMakeLists.txt
├── source/
│   ├── x2uhj_ids.h          # plugin UID (no parameter IDs — stateless)
│   ├── x2uhj_processor.h    # FAUST REFERENCE block + quadrature pair + matrix
│   ├── x2uhj_processor.cpp  # IAudioProcessor + DSP
│   └── version.h
├── tools/                   # analysis harness (§6)
└── doc/
    └── references/          # archived PDFs (§9)
```

Registered in root `CMakeLists.txt` via `add_subdirectory(plugins/x2uhj)`.
No `resource/` (no GUI in v1; processing-only, like the matrix utilities).

`x2uhj_processor.h` opens with:
`// FAUST REFERENCE (seam.ambisonics.lib): x2uhj` plus the resolved matrix and
the note that the canonical Faust function is being corrected as part of this
work (§10).

## 9. Reference archival

Six of the ten source PDFs live on an external volume (`/Volumes/Aleph`); four
in iCloud. Copy the relevant primary sources into
`plugins/x2uhj/doc/references/`. Confirm during implementation whether all ten
are archived or only the directly-cited subset (Super Stereo guide, Envelop
implementation, NYCEMF C-format, Gerzon 1983, Gerzon US4095049).

## 10. Backbone deposit — Faust library update

After the C++ port, update `seam.ambisonics.lib` with the **corrected,
canonical** UHJ function (currently absent). This resolves the divergence
between the two original `.dsp` files in the library itself. The hand-port is
also the review step (the value the `CLAUDE.md` convention describes).

## 11. Out of scope (v1)

- UHJ → B-format reconstruction (the inverse super-stereo decode, Fig. 4 of the
  guide). Future.
- Higher orders (explicitly not of interest; FOA only).
- Full VST-loading measurement host (only the 1:10 harness embryo here).
- GUI, parameters, presets.

## 12. Open items to resolve during implementation

- Final equiripple band edges and order (driven by harness evidence).
- Confirm exactly which empirical-coefficient biquad topology the 2023 work
  used (for an accurate comparison in the harness) — check the gen~/Envelop
  source if needed.
- Decide full vs. subset PDF archival.
