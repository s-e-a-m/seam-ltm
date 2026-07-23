# ADDELAY — Air-Absorption Companion to ddelay — Design

Status: approved (brainstorming, 2026-07-23).
Scope: one new plugin, `addelay`.
Doc debt tracked in memory `project_addelay_documentation_debt`; source note
`plugins/ddelay/doc/roadmap.md`.

## Purpose

`ddelay` turns a distance into an integer-sample delay and stops there: it
models exactly one consequence of distance — arrival time. A loudspeaker twenty
metres away is not only late, it is **duller** — air attenuates high frequencies
as sound travels through it, and the effect is audible well inside the 30 m
`ddelay` already covers. Time alignment alone makes a far source *punctual*, not
*far*; the ear reads an undimmed top end as nearness and the depth illusion
collapses.

`ADDELAY` is the companion that adds the missing half: the distance-dependent
spectral tilt. It **inherits ddelay's metres→samples + nextPrime core** (so the
alignment stays exact) and adds a minimum-phase air-absorption filter on top.

**Two halves of "distance", split cleanly by phase type.** Distance has a
bulk-propagation delay (linear phase — the speed of sound) and an
absorption/dispersion (minimum phase — Kramers-Kronig linked to the magnitude).
`ddelay` already takes the first half (the linear-phase bulk delay). ADDELAY's
filter is exactly the **minimum-phase residual** that completes the model. This
is why minimum-phase is not merely a cheap choice here — it is the correct one.

## Why a separate plugin (not an option in ddelay)

`ddelay`'s promise is exactness — integer samples, no interpolation, no
crossfade, no smoothing; a tool for aligning real loudspeakers. A filter in that
path changes what the plugin is *for*. ADDELAY inherits the metres→samples core
and adds the filter, so the alignment tool stays as sharp as it is and the
colouring tool is its own thing. Both plugins ship; neither replaces the other.

## The design conversation that shaped this (recorded)

A poly-phase idea (deliberately dispersing the 4 cones' phases around fc) was
raised and **correctly dropped**: the phase diversity that makes a STONE a rich
spherical source lives in the **signal** (TETRAREC's 4 spaced omnis, or the
future tetrahedral encoder), not in ADDELAY. ADDELAY's job is to attenuate
magnitude while disturbing the signal's existing phase as little as possible.
Because ADDELAY applies the **same filter to all four channels** (one distance),
the inter-channel phase relationships that carry the spatial image are preserved
**by construction** — a common filter cannot change relative phase. Dropping the
poly-phase also simplified the honesty problem: the whole plugin is now
sourced-not-invented (magnitude = ISO physics, phase = the minimum phase that
physics implies), with no invented layer to flag.

## Parameters

Five controls. Pressure is fixed at 1 atm (101.325 kPa); temperature and
humidity are the two inputs that dominate α over the plugin's range.

| Param | ID | Range | Default | Unit | Notes |
|---|---|---|---|---|---|
| Distance | 100 | 0 … `kMaxDistance` (30 m) | 0 | m | inherited from ddelay; drives the delay, α·d, and the spreading gain |
| Temperature | 101 | −20 … 50 | 20 | °C | ISO input T |
| Relative Humidity | 102 | 0 … 100 | 50 | % | ISO input h_rel; matters as much as distance |
| Topology | 103 | {Shelf, Cascade} | Shelf | — | `StringListParameter`, live-switchable filter fidelity |
| Spreading | 104 | {Off, On} | Off | — | optional broadband 1/r geometric-spreading attenuation |

The absorption filter is **redesigned whenever Distance, Temperature or Humidity
changes** — a control-rate recompute in the parameter-change handler, never in
the sample loop (the loop only runs the current coefficients). This mirrors how
`hilbert`/`x2uhj` redesign their filters on a sample-rate change; here the
triggers are distance/T/RH. The spreading gain (below) is likewise a scalar
recomputed on a Distance change.

## The filter

### Magnitude target — ISO 9613-1 (sourced, not invented)

The attenuation over distance `d` is `A(f) = α(f, T, h_rel) · d` decibels, where
α is the atmospheric absorption coefficient in dB/m from **ISO 9613-1**. The
filter's magnitude response is `10^(−A(f)/20)` — naturally ≈ 0 dB at low
frequency (α→0) and rolling off the highs more as distance grows, so **no
normalization is needed**: it is intrinsically a distance-controlled low-pass.

α depends on frequency and on the O₂/N₂ relaxation frequencies, which depend in
turn on temperature, water-vapour molar concentration (from relative humidity,
temperature and pressure), and pressure. The structure is:

```
α = 8.686 · f² · {  1.84e-11 · (pa/pr)^-1 · (T/T0)^(1/2)
                  + (T/T0)^(-5/2) · [ 0.01275 · e^(-2239.1/T) / (frO + f²/frO)
                                    + 0.1068 · e^(-3352.0/T) / (frN + f²/frN) ] }
```

with `frO`, `frN` (oxygen/nitrogen relaxation frequencies) and the water-vapour
term derived from T, h_rel, p.

> **This transcription is from memory and is NOT authoritative.** The
> implementer reads **ISO 9613-1** directly and verifies every constant,
> exponent and the water-vapour / relaxation-frequency sub-formulas against the
> standard, with the atmospheric-absorption literature (Bass, Sutherland,
> Zuckerwar) behind it. A plausible-sounding tilt invented at the keyboard would
> be indistinguishable to a casual listener and useless to a student reading the
> code to learn what air does — which is the whole point.

### Phase — minimum phase

Both topologies are realized as **minimum-phase** IIR. This is physically
correct (air absorption is minimum-phase), adds **zero latency** and no
pre-echo, and — applied identically to all four channels — leaves the
inter-channel phase untouched. A linear-phase FIR is explicitly rejected: it
adds latency (breaking ddelay's sharpness), pre-rings transients (which air
never does), and imposes a flat phase air does not have.

### Two topologies (both minimum-phase, same ISO target)

Selectable live via the Topology control, precedent set by `hilbert` (RBJ vs
Niemitalo). Both fit the **same** `α·d` target, so the A/B isolates fit fidelity
alone — the student hears "a gesture" against "the physics" on the transiting
signal, in the room, on the piece.

- **Shelf** — a single first-order minimum-phase low-pass shelf, its corner and
  depth fitted to the α·d curve. One coefficient set, transparent to read and
  document; captures the gross "farther = darker" tilt but cannot follow the
  ISO curve's continued steepening.
- **Cascade** — a short (2–3 section) minimum-phase cascade of first-order
  shelves/one-poles, fitted to the α·d curve across 20 Hz–20 kHz. Tracks the ISO
  curve across the band; still cheap, still zero-latency.

**Fit method (design level):** for the current (d, T, h_rel), sample the target
`A(f)` on a log-frequency grid and fit the shelf / cascade parameters by
least-squares in dB over log-frequency (the exact algorithm is a plan/impl
detail). Recomputed at control rate on parameter change.

### DSP flow (per sample, per channel)

```
in → [ ddelay core: integer-sample delay = nextPrime(round(d·SR/331.4)) ]   (bulk, linear phase)
   → [ air filter: current minimum-phase coefficients (shelf or cascade) ]  (residual, min phase)
   → [ × spreading gain (scalar, if Spreading = On) ]                       (broadband level, no phase)
   → out
```

All four channels share one distance → one delay → one filter coefficient set
→ one spreading gain.

## Geometric spreading (optional)

Distance changes level by **two independent laws**, and ADDELAY keeps them
separate because they are separate physics:

- **Geometric (1/r) spreading** — a *broadband* level drop, −6 dB per doubling
  of distance, at every frequency including DC. It dominates the *level* change
  over the plugin's range.
- **Atmospheric absorption** (the filter above) — a *frequency-dependent* loss,
  ≈ 0 dB at low frequency, growing with f. It dominates the *timbre* change.

The absorption filter alone leaves DC at unity: as specified, ADDELAY with
Spreading = Off makes a source **duller but not quieter**. The **Spreading**
toggle adds the broadband 1/r attenuation so a far source is also softer — the
full "far", quieter *and* duller.

**Definition (no singularity, no boost):** referenced to a fixed **1 m** (the
standard SPL reference distance, a documented constant like the 1 atm pressure),
attenuation-only:

```
spreadingGain_dB(d) = −20 · log10( max(d, 1 m) / 1 m )
```

So at d = 0 → 0 dB (no infinity), below 1 m → unity, beyond 1 m → −6 dB/doubling.
It is a **scalar broadband gain**: no phase, it commutes with the filter, and it
is applied identically to all four channels — so it never touches the
minimum-phase / inter-channel-phase story. The 1 m reference is fixed (not a
control); enabling spreading costs exactly one control, the toggle.

**Honesty / pedagogy note:** true 1/r is dramatic — from 1 m to 30 m is ≈ −29 dB,
far more than absorption. Turning Spreading on makes that audible immediately,
which is the point: the student hears that spreading governs level and
absorption governs colour. In a sound-reinforcement use (the STONE
re-amplifying), the composer typically wants presence kept, so Spreading
defaults **Off** and the broadband level stays the fader's job unless explicitly
invoked.

## I/O and state

- **Bus:** 4ch → 4ch (`kAmbi1stOrderACN`), same as ddelay.
- **State:** a brand-new plugin, no legacy format to be compatible with. Store
  the five parameters (Distance, Temperature, Humidity normalized; Topology
  index; Spreading toggle) with the suite's raw-blob `getState`/`setState`
  idiom. (Note: this
  family shares the state-read short-read caveat in memory
  `project_vst3_state_shortread_rotation_family`; ADDELAY, being new, is not a
  widening case, so it is unaffected — it simply follows the family idiom.)

## Faust spec and documentation (the "Faust is the spec" convention)

- **Faust:** the delay half is already specified (`seam.math.lib::isos` +
  `imt2samp`, `sff.np` nextPrime — inherited from ddelay). Add the **air-filter
  half** as a new function to an appropriate `seam.*` library: the α(f, T, h_rel)
  formula and the two minimum-phase filter designs (shelf, cascade). Generate
  its mathdoc (`-svg` + PDF) with `tools/gen-faust-doc.sh`, as `ddelay.dsp`
  already does. The exact library file is a plan decision.
- **C++:** hand-ported to match the Faust spec, with the processor header's
  `FAUST REFERENCE` block citing ISO 9613-1 + Bass/Sutherland/Zuckerwar the way
  the suite cites Blumlein, Gerzon and Linkwitz.
- **doc/math/** (English, formal): the ISO 9613-1 model — α(f, T, h_rel) in
  dB/m, the assumed atmospheric conditions and the exposed T/RH ranges, and the
  fit method for each of the two topologies. Provenance is the deliverable.
- **doc/study/** (Italian, optional): a narrative diary on what air does to
  sound over distance, and on A/B-ing the two topologies in the room.
- **README:** ADDELAY as a new plugin in the measurement/processing family.

## Window (UI standard)

Five controls (three sliders + two selectors/toggles), following
`doc/style/ui-style.md`, whose S/L threshold turns on control count. Five sits
at the boundary; the UI task settles S single-column (taller, like ddelay) vs a
light L, but the zone assignment is fixed:

- HEADER: `SEAM ADDELAY` + subtitle (e.g. "Distance Air-Absorption Delay") +
  info line.
- SETUP: the **Topology** selector (`COptionMenu`), placed near the top as
  `hilbert` places its Topology.
- OPS: the **Spreading** toggle (a checkbox or 2-item selector — an on/off mode,
  the operational-vocabulary zone).
- FINE: Distance, Temperature, Relative Humidity sliders (label / slider /
  value blocks).
- FOOTER: the logo.

Lints clean under `tools/check-uidesc.py`; a screenshot is retaken after a host
check (the `check_screenshots` rule requires `docs/img/addelay.png` referenced
in the README).

## Decisions (as agreed, 2026-07-23)

| Decision | Choice |
|---|---|
| Shape | Separate plugin; ddelay delay core + air filter; 4ch, one distance |
| Atmospheric inputs | Temperature + Relative Humidity controls; pressure fixed 1 atm |
| Magnitude | ISO 9613-1 α·d, sourced from the standard, verified constant-by-constant |
| Phase | Minimum phase (the residual completing ddelay's linear-phase delay); FIR rejected |
| Inter-channel phase | Preserved by construction (same filter on all 4 channels) |
| Topology | BOTH shelf and cascade, live-switchable (hilbert precedent), same ISO target |
| Geometric spreading | Optional broadband 1/r attenuation, toggle (default Off), fixed 1 m reference, attenuation-only |
| Faust | New air-filter function + mathdoc; delay half already specified |

## Out of scope

- The poly-phase / per-cone dispersion idea (dropped: phase belongs to the
  signal, not to ADDELAY).
- Pressure as a control (fixed at 1 atm).
- Any change to `ddelay` itself.
