# ltglide Phase 3b-i — Glissando Tone-Burst Generator — design

Date: 2026-07-01
Status: approved (brainstorming) — implementation plan next
Parent: docs/superpowers/specs/2026-06-30-ltburst-phase3-fixed-generator-design.md
Faust spec: docs/superpowers/specs/2026-06-30-ltburst-glissando-sweep-design.md
Project spec: docs/superpowers/specs/2026-06-18-ltburst-linkwitz-tone-burst-design.md

## Context: the "glissando calibration" system

The fixed-frequency generator `ltburst` is shipped and teaches one thing well:
*what a Linkwitz shaped tone burst is*.
The glissando is a different, **operative** object: it drives an actual
loudspeaker ("stone") calibration in a hall.

A glissando played on its own reveals nothing to the ear — it happens too fast.
Its value appears only through a **microphone plus an analysis practice**: you
observe how the *known* deterministic glissando was deformed by the
electro-acoustic chain, then take corrective action and iterate until satisfied.

The full system is therefore **two peer-aware plugins** (the pattern introduced
by `multipink` / `multipink_pool`):

| | Generator (operative) | Receiver (measurement) |
|---|---|---|
| DSP | C++ port of `sweepfreq` + `glissburst` | regenerate the reference + analyse |
| Marker | Dirac head/tail (±5 s) | detect Δt from the Dirac → gate anchor |
| Transport | loop / manual trigger | average nodes across passes (SNR) |
| State | *publishes* `(f0,f1,t, sample-origin)` | *reads* the same state → sync |
| Output | mono, into the stone chain | table freq/gain/Q, ≤8 bands, pink target |

Because the generator is deterministic (given `f0`, `f1`, `t`), every burst falls
at a known, knowable instant — a node of the deterministic glissando.
The receiver shares that determinism, so it needs no data other than the known
reference and the received stream to recover the transfer function sampled at the
nodes, from which the correction toward the target is computed.

### System-level facts settled in brainstorming (bind later phases)

- **Target** = pink, −3 dB/octave (equal energy per octave).
- **Reference delivery** (receiver) = internal regeneration synchronised via the
  shared static module, not an audio send.
- **Acoustic delay Δt** is measured from the head Dirac (the determinism gives
  the departure instant, not the arrival); the Dirac also anchors the
  reflection-rejection time gate and yields a broadband impulse-response
  sanity-check for free.
- **Deliverable now** = the receiver *describes* the correction as a
  ≤8-band parametric-EQ table (freq/gain/Q) that Giuseppe dials by hand into the
  power amplifier's 8-band parametric EQ.
  A **magnitude-only** measurement suffices for that goal (a parametric EQ is
  minimum-phase and corrects magnitude); full complex phase inversion is an
  optional diagnostic, deferred.
- **Auto-aligning** plugin (measures and applies the curve directly on a stone)
  is a later, final phase.
- **Chain-under-test caveat**: the signal path is
  `ltglide → m2xhgr → bamodulex → stone`; the measured deformation includes
  `m2xhgr` and `bamodulex`.
  For the transfer function to be meaningful that chain must be LTI/frozen during
  a pass (`bamodulex` is a modulator: if it modulates, it is not time-invariant).
  This is a receiver-phase constraint; it does not affect the generator.

## Decomposition

The system is too large for one spec, so it splits into two sub-projects, each
with its own spec → plan → implementation cycle:

- **3b-i — glissando generator `ltglide`** (this document): the operative,
  standalone deterministic signal source.
- **3b-ii — receiver / measurement plugin** (separate spec, later): the
  peer-aware consumer, Δt detection, gating, narrow-band magnitude, averaging,
  deformation curve, pink-target correction, 8-band fit and display.

The generator is useful on its own: once it emits the deterministic glissando and
the Dirac markers, a recording of the microphone can be analysed offline.
The receiver depends on the generator (it needs the shared determinism), so the
generator is built first.

## This sub-project: `ltglide` (3b-i)

### Scope and boundaries

- A **standalone operative generator**: deterministic glissando of tone bursts
  plus head/tail Dirac markers, mono, at the head of the stone chain.
- **Peer-aware synchronisation is out of scope here.**
  CLAUDE.md forbids retrofitting peer awareness without a real consumer, and the
  consumer (the receiver) does not yet exist.
  The determinism already lives in the *signal*; offline analysis works from a
  recording.
  Shared-state synchronisation arrives in 3b-ii, when there is a reader.
- New plugin, **not** a mode inside `ltburst`.
  `ltburst` stays the pristine teaching object ("understand the burst");
  `ltglide` is the operative instrument ("calibrate with the glissando").
- **No routing logic and no stone index in the plugin.**
  `ltglide` is mono and sits at the head of one stone's chain
  (`ltglide → m2xhgr → bamodulex → stone`); one instance calibrates one stone,
  routing is a DAW/chain concern.

### Shared DSP core

Factor a shared, SDK-free, header-only DSP unit
`plugins/ltglide/source/linkwitz_dsp.h` containing:

- `ShapedBurst` — reused unchanged from the current `ltburst_dsp.h` (fixed-freq
  burst; a constant frequency in gap mode is the degenerate glissando).
- `GlissBurst` — the C++ port of the Faust `sweepfreq` + `glissburst`
  (Approach A, retriggered grain): a grain ramp that resets at each onset, a
  sample-and-hold that latches the swept frequency at that instant, the carrier
  and Hann window both derived from the reset ramp at the latched frequency, and
  the passo/gap period selection.

Both classes are unit-tested with doctest and checked for numerical parity
against the Faust reference, as `ShapedBurst` already is (parity ≈ 1e-13).
`ltburst` keeps working unchanged; it may include the shared header instead of a
local copy (a mechanical, behaviour-preserving change confirmed by its doctest).

### Parameters

| Param | Range / values | Default | Notes |
|---|---|---|---|
| Level (dBFS) | −60 … 0 | −20 | reused from ltburst; also the ceiling for the Dirac |
| f0 (Hz) | 20 … 20000 | 20000 | sweep start; log taper |
| f1 (Hz) | 20 … 20000 | 20 | sweep end; log taper |
| smode | linear / exponential | exponential | equal octaves per unit of progress |
| dmode | passo / gap | gap | onset-fixed vs gap-fixed grain timing |
| delta (s) | grain spacing | 0.3 | passo: onset→onset; gap: end→onset |
| t (s) | 2 … 120 | 20 | sweep duration; governs the rate of progress `p` |

`N` (burst cycle count) is **fixed at 5** (canonical Linkwitz, the `glissburst5`
wrapper) and is not exposed.

### Transport state machine and Dirac markers

A full **pass** is the timeline:

```
Dirac-head → 5 s silence → glissando (p: 0 → 1 over t) → 5 s silence → Dirac-tail
```

- The transport **owns** the progress signal `p` (in the Faust spec `p` was
  supplied externally via `os.phasor`); the plugin generates `p` from `t` and the
  transport state, and feeds `sweepfreq(f0,f1,smode,p)` into `GlissBurst`.
- **Trigger** (momentary button) runs one pass.
- **Loop** (toggle) repeats `pass → wait W → …`.
- Dirac = a single full-scale-relative sample at the Level ceiling; head and tail
  bracket the glissando so the receiver can later measure Δt and check for clock
  drift across the pass.

State machine (per pass): `Idle → HeadDirac → LeadSilence → Glissando →
TailSilence → TailDirac → (Loop ? Wait → HeadDirac : Idle)`.
A pass-start latches all parameters for that pass so a mid-pass parameter change
cannot discontinue the running glissando (consistent with the `ltburst` phase
handling).

### GUI and documentation

- VSTGUI editor consistent with `ltburst` (native-size logo, honest readout
  precision), exposing the parameters above plus the Trigger button and Loop
  toggle.
- `doc/study` Section 6 (Italian narrative diary): the C++ port of the grain
  engine and the transport state machine.
- `doc/math` English formal documentation authored at the end of Phase 3.

## Verification (gates)

- `GlissBurst` doctest passes and matches the Faust `glissburst` reference to
  ≈1e-13 in both passo and gap modes.
- `ltburst` doctest still passes after the header factoring (behaviour
  unchanged).
- The plugin builds and passes the SMTG VST3 validator.
- A recorded pass shows the expected structure: head Dirac, silence, glissando
  with per-grain single-frequency bursts, silence, tail Dirac.
- Mono bus declared; multichannel rejected (as in `ltburst`).

## Out of scope (now)

- The receiver / measurement plugin (3b-ii): Δt detection, gating, narrow-band
  magnitude, averaging, deformation curve, pink-target correction, 8-band fit,
  display.
- Peer-aware shared-state synchronisation between generator and receiver.
- Full complex phase analysis and the auto-aligning correction plugin.
