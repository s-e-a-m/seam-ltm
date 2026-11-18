# BASTOOGE — design

Date: 2026-11-18.
Who: Giuseppe Silvi (request, measurements, decisions); the agent (Claude: analysis, drafting).
S.T.OOGE is developed with Francesco Palmieri, guitarist.

## Intent

BASTOOGE is the decoder that belongs to one loudspeaker body: the S.T.OOGE mk2g, a tetrahedral guitar loudspeaker with four 8" cones in a common bass-reflex chamber.
It is `bamodulex` (Gerzon's BA-module, AmbiX → LFU · RFD · RBU · LBD) with the filters that a per-face equaliser cannot express: one filter on W, one filter shared by X, Y and Z, placed before the matrix.

The question it answers was opened in the AES Italia 2026 paper *Pensare tetraedrico oggi*: the paper states that the loudspeaker compensation belongs to the decoding, and `bamodulex` omits it by design.
On 2026-11-18 a measurement session at LEAP (Rome) on the finished mk2g established what the compensation is.

## Why a decoder, and not the amplifier

The amplifier (Thomann Quadro 500 DSP) carries one filter set, identical on the four faces.
An identical filter on every face commutes with the orthogonal matrix, so it acts identically on W, X, Y and Z.
In a common chamber W and the dipoles are different physical objects.
Only W compresses the air and drives the ports; in a dipole mode the summed cone volume is zero, the internal pressure is near zero, and the cones work unloaded, without the reflex.

The measurements confirm this, mode by mode, at one position (frontal AB pair at 1 m, about the critical distance of the 83 m³ room):

- The amplifier filters shift W, X and the decorrelated case by the same curve within about 1 dB: they commute with the matrix.
- X and Y, measured with the body rotated by 90° and the microphones fixed, coincide within about 1 dB: one filter serves the three dipoles, as the tetrahedral symmetry requires (the three dipoles span an irreducible representation of the rotation group, and by Schur's lemma the body acts on them as a scalar).
- The decorrelated case equals the power mean of the four modes at the microphone within ±0.6 dB from 63 Hz to 20 kHz.
- The dipole radiates 11–14 dB less than W from 63 to 160 Hz, about 7 dB less at 200 Hz, 3–5.5 dB less at 250–315 Hz, 0–3 dB less from 400 Hz to 2.5 kHz, and the same above 3 kHz.

That last ratio is the part no amplifier setting can correct, and it is what BASTOOGE carries.

The division of labour follows:

- **BASTOOGE** carries the constants of the model: the W/dipole balance and the dipole cut.
  They are the same for every mk2g.
- **The amplifier** carries what is common to the four modes and specific to the unit, the room and the concert: the cone roll-off, the reflex hump at 160 Hz, the bass extension.
  It is calibrated with `strx`, after BASTOOGE is in the chain.

## Goals

- A hand-written C++ VST3 plugin in `seam-ltm`, AmbiX 4 ch → tetrahedral 4 ch, whose Faust specification lives in `seam.ambisonics.lib`.
- One free parameter: the dipole cut frequency.
  The W filter follows it, recomputed so that the crossover between W and the dipoles stays coherent at every setting.
- A test selector that produces each mode alone, through the decoder, so that `strx` measurements are repeatable without routing tricks.
- A bypass that removes the filters and leaves the matrix, for A/B against `bamodulex`.

## Non-goals

- A menu of loudspeaker models (S.T.ONE, STONED, S.T.ON3L).
  The mk2g constants sit in one isolated table; a model menu comes when a second body has been measured the same way.
- Any change to the amplifier calibration, to `bamodulex`, or to the encoders (`lr2xhgr`, `m2xhgr`, `abmodulex`).
- Compensation of the phase of the W bell itself (see *Open*).
- Peer awareness across instances.

## Anti-goals

- Faust-generated code under `plugins/bastooge/source/`.
- A control that duplicates the encoder: a dipole gain or a "width" in the decoder breaks the separation the project holds, where the form of the source belongs to the encoder (`lr2xhgr` already has the A1–A3 trims).
- A test mode that silently skips the decoder filters: on 2026-11-18 one pair of mode measurements was taken through the soundcard matrix, outside the decoder, and had to be discarded.

## Constraints

- The suite rules in `CLAUDE.md`: Faust is the specification, C++ is the deliverable, the Faust reference is cited in the processor header.
- The window standard in `doc/style/ui-style.md`, starting from `plugins/_template/resource/_template.uidesc`, linting clean under `tools/check-uidesc.py`.
- The registry `doc/plugins.toml` is the single source of the README tables and of the site page; the plugin count in `CLAUDE.md`, the README and the registry tests moves from twenty-one to twenty-two.
- The append-only state contract of the suite.
- Filters designed at the host sample rate; nothing in BASTOOGE depends on a fixed rate.
- Every number in the table traces back to a file: measurements in `dream-stone/stooge/mk2g/misure/`, analysis in `dream-stone/logs/2026-11-18-stooge-mk2g-primo-ruggito.md`, the first proposal in `dream-stone/stooge/mk2g/decoder-proposta.md`.
  `dream-stone` is not public, so the measured ratio table (31 third-octave values) is also copied into the plugin's `doc/`, with its provenance, so that the public repository stands on its own.

## Approaches considered

Each must answer one question: when the dipole cut moves, what happens to the W filter?
The first proposal derived the W bell (250 Hz, Q 1.4, −5 dB) assuming the dipoles fully present; near the cut the high-pass still attenuates them, the two cuts add, and a coherent source showed a residual dip of about 1.5 dB at 250 Hz.

- **A. Everything fixed, the residual to the amplifier.**
  Fixed bell, Linkwitz-Riley high-pass at the chosen cut, matched all-pass on W.
  Simplest; the residual moves with the cut, so every change of the cut asks for a new amplifier calibration.
- **B. The bell derived from the cut** — chosen by Giuseppe, 2026-11-18.
  A written target for the W attenuation, weighted by how much the high-pass lets the dipoles through, fitted offline with a bell for each cut value; the plugin interpolates the fitted table.
  It removes the residual at its root and keeps every filter a readable bell, high-pass or all-pass.
- **C. An analytic W filter fitted directly to the measured ratio**, without bells.
  Most exact, but opaque in C++: it fails the suite's purpose of reading DSP from source.
  Discarded.

## Design

### Signal flow

```
AmbiX in: A0 (W), A1 (Y), A2 (Z), A3 (X)
  │
  ├─ test selector (Normal / W / X / Y / Z)
  │
  ├─ W:        bell(fc) → all-pass(fc)
  ├─ Y, Z, X:  high-pass LR4(fc), the same on the three
  │
  └─ bamodulex matrix → LFU, RFD, RBU, LBD
```

Bypass removes the bell, the all-pass and the high-pass, and keeps the matrix and the test selector.
With bypass on, BASTOOGE is `bamodulex`, and the test selector gives the bare modes.

### The dipole high-pass

Fourth-order Linkwitz-Riley (two cascaded second-order Butterworth sections, Q = 1/√2, both at fc), the same on Y, Z and X.
Linkwitz-Riley rather than Butterworth because its phase is exactly that of a second-order all-pass, which W can then match (next section).
On 2026-11-18 the cascade of two second-order high-passes was the one actually measured (Apple AUNBandEQ), and it behaved as computed.

### The W all-pass

Second-order all-pass at fc, Q = 1/√2.
It matches the phase of the dipole high-pass at every frequency, by the Linkwitz-Riley identity HP_LR4 + LP_LR4 = AP₂.
It matters for coherent sources: `lr2xhgr` puts the same signal on W and X, and at the microphone their pressures add in amplitude.
On 2026-11-18 adding the all-pass on W raised the band at 250 Hz by 2.2 dB with an `lr2xhgr` source, closing most of the measured dip.

### The dipole/W ratio: a physical model fitted to the measurement

The measured ratio (the power mean of the three axes at the frontal pair, (X_lobe + 2·X_null)/3, over W; table in `doc/`) carries two things that do not belong to the model constant.
Below 200 Hz it carries the reflex hump of W at 160 Hz, which belongs to W alone and therefore to the amplifier (W is the only mode left there).
Above 400 Hz it carries ±2 dB of single-position, third-octave ripple from the room.
Fitted directly, these two pull a single bell to a wide mid-band cut that jumps with fc (found by the builder check, 2026-11-18).

BASTOOGE therefore uses a physical model of the ratio:

```
r(f) = x² / (1 + x²),   x = f / f1
```

This is the radiation law of a compact source: the dipole radiates (ka)² times the monopole while ka is small, and as much as the monopole once ka is large.
Fitted by least squares in dB on the third-octave centres from 200 Hz to 6.3 kHz (so that the W hump stays out), it gives **f1 = 359 Hz**, RMS error 1.2 dB.
f1 is where the dipole is 3 dB under W; read as ka = 1, it puts the effective radius of the radiating body at about 15 cm.
Below 200 Hz the model extrapolates the (ka)² law: −7.9 dB at 160 Hz against a measured −13.8 dB, the difference being the W hump that the amplifier takes.
The fitting script in `dream-stone` writes f1 together with the bell table, and its test checks that the model stays within 1.5 dB RMS of the measurement over the fit band.

### The W bell, as a function of fc

Let r(f) be the model above.
Let |H(f; fc)|² be the power response of the dipole high-pass.
The target attenuation of W, in power, is

```
T_W(f; fc) = 1 − |H(f; fc)|² · (1 − min(r(f), 1))
```

Where the high-pass lets the dipoles through, T_W = r: W comes down to the level the dipoles reach.
Where the high-pass has removed them, T_W = 1: W alone is left alone.
In between, the transition follows the high-pass itself.
The target does not depend on the source: it balances the modes, whatever share of power the source puts in the dipoles.

For each fc on a grid over the parameter range, one peaking biquad (f0, Q, gain ≤ 0 dB) is fitted to T_W by least squares in dB, on a dense logarithmic grid from fc/2 to 6.3 kHz.
The fit runs offline, in a script in `dream-stone` (`src/tools/stone/`, with tests, Italian like the rest of that repository), which writes the table `data/stooge-mk2g-bastooge.dat`.
The table is copied into `seam.ambisonics.lib` and into the C++ header, with the provenance and the date.

A trial of this fit (agent, 2026-11-18) gives a smooth, monotone table, with the bell sitting at about twice fc:

| fc | bell f0 | Q | gain | fit error, RMS / max |
|---|---|---|---|---|
| 100 Hz | 222 Hz | 0.84 | −5.0 dB | 0.28 / 0.64 dB |
| 125 Hz | 260 Hz | 0.87 | −4.1 dB | 0.23 / 0.52 dB |
| 160 Hz | 310 Hz | 0.91 | −3.1 dB | 0.18 / 0.40 dB |
| 200 Hz | 365 Hz | 0.94 | −2.4 dB | 0.14 / 0.31 dB |
| 250 Hz | 433 Hz | 0.96 | −1.8 dB | 0.10 / 0.23 dB |

At fc = 160 Hz the bell is gentler and higher than the one tried in the room (250 Hz, Q 1.4, −5 dB), and this is the purpose of approach B: the first bell was derived with the dipoles fully present, so at 250 Hz it cut W by 5 dB where the high-pass still held the dipoles 1.3 dB down, and the measurement showed the resulting dip.
The new bell cuts W by about 2.7 dB at 250 Hz (and 1.2 dB at 160 Hz), which is expected to close the residual measured with `lr2xhgr`.
The script's test checks that the table is monotone in fc for f0 and gain, and that the fit error stays under 1 dB everywhere.
If the final script departs from the trial table by more than 1 dB in gain or a third of an octave in f0 at any grid point, the plan stops and the departure is brought to Giuseppe before any table reaches Faust or C++.

### Parameters

| Parameter | Range | Default | Note |
|---|---|---|---|
| Dipole cut fc | 100–250 Hz, logarithmic | 160 Hz | moves the high-pass, the all-pass and the fitted bell together |
| Test | Normal, W, X, Y, Z | Normal | ordinary saved state, like every parameter; the GUI shows a test mode loudly (see GUI) |
| Bypass | off / on | off | filters out, matrix in |

The range comes from the session: below 100 Hz the dipoles are 11–14 dB under W and the cones work unloaded; above 250 Hz the dipoles already reach W within a few dB, and cutting there removes directivity the body does produce.
Higher fc gives more headroom and less excursion, for loud playing; lower fc lets the directivity reach lower, for quiet playing.

### Test selector

In a test mode, input channel 1 (A0) feeds the chosen mode alone, with gain 2, and the other three inputs are ignored.
The gain of 2 compensates the ½ of the matrix, so each face receives the input amplitude in every test mode: the same voltage per face in every case, the condition of the `dream-stone` measurement protocol (`stooge/mk2g/misure-modi-protocollo.md`).
Test modes go through the decoder filters unless bypass is on.
A mono `multipink` stream into channel 1 then gives, with one menu, the W, X, Y and Z measurements of 2026-11-18, decoded or bare.

### GUI

Zones as the standard prescribes; the format follows the control count (one slider, one menu, one toggle).
FINE: the dipole cut slider, the test menu, the bypass toggle.
FOOTER: a readout of the filters in force (bell f0, Q, gain; the cut), so that a student sees the bell follow the cut; and a status line that names any test mode in capitals, and adds a warning when a dipole test runs with bypass on, because the cones then take the bass unloaded.
Whether bypass maps to an existing word of the operational vocabulary or stays a FINE toggle is for the builder to settle against `ui-style.md`; POWER does not fit, since there "off" means silent.

### Tests

- The matrix: with bypass on and test Normal, the output equals `bamodulex` sample for sample.
- The phase match: the phase of the W all-pass equals the phase of the dipole high-pass within a tight tolerance across 20 Hz–20 kHz, at several fc and sample rates.
- The test selector: in each test mode the four faces carry the input amplitude, with the signs of the matrix (W + + + +, X + + − −, Y + − − +, Z + − + −, in the order LFU, RFD, RBU, LBD).
- The table: interpolation between grid points is monotone and continuous; at grid points the coefficients equal the table.
- State: the append-only contract, as in the other plugins.
- Parity with the Faust specification, with the suite's existing A/B tools (`tools/faust-ab-arch.cpp`).
- In `dream-stone`: the fitting script is tested against the measurement files it reads, and its fit at 160 Hz is checked against the first proposal.

### Validation in the room

Repeat the 2026-11-18 `lr2xhgr` measurement (two decorrelated `multipink` streams into `lr2xhgr`, divergence 135, trims at 0) with BASTOOGE in place of the AUNBandEQ chain.
Expected: the residual dip at 200–250 Hz within about 1 dB of the power prediction, at fc 160 Hz and at the ends of the range.
Then the amplifier calibration with `lr2xhgr` in the chain, and listening with Palmieri across fc.

## Open

- **The phase of the bell.** The bell is minimum-phase and only W has it; near 250 Hz it leaves a few degrees of mismatch between W and the dipoles.
  The measurements do not show it as a problem yet; a later version can give the dipoles a matching all-pass if a coherent-source measurement asks for it.
- **One position, one room.** The ratio r(f) comes from the frontal pair at 1 m in the LEAP stage area; a second position or a second room would confirm f1.
- **The W hump at 160 Hz** is left to the amplifier by construction of the model; at low fc settings the amplifier calibration has to account for it.
- **Z was not measured.** By symmetry it equals X and Y; the floor is room, not body.

## Left to the builder

- Biquad topology, coefficient formulas, and how coefficients change when fc moves (smoothing or crossfade, without clicks).
- The density of the fc grid and the interpolation scheme, within the continuity test above.
- The GUI layout within the standard, and the treatment of bypass in the vocabulary.
- File names and the split of `source/` beyond the canonical layout of `bamodulex`.

## Calls made by the agent

Settled after the builder check (2026-11-18), for Giuseppe to review while reading:

- The bell is fitted to a physical model of the dipole/W ratio, r = x²/(1 + x²) with f1 = 359 Hz, instead of the raw third-octave measurement, so that the W reflex hump (amplifier) and the room ripple (one position) stay out of the decoder.
- The fit band starts at 200 Hz, below the W hump.
- The dipole cut range stays 100–250 Hz, default 160 Hz.
- The test selector is ordinary saved state; the GUI makes a test mode impossible to miss.
- The phase of the bell is left open.
