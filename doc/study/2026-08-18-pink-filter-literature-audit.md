# Pink-filter literature audit — research brief

Written 2026-08-18, after `multipink` was found to emit a spectrum that is
**3 dB short at 20 Hz when the session runs at 96 kHz**. The methodology
mirrors the quadrature/allpass study, where surveying alternatives changed the
answer.

**This file is the prompt.** Paste it from the rule below, whole, into a
research assistant with web search. Everything above the rule is context for
us; everything below it is addressed to the assistant.

---

You are a research assistant. Use web search. Prefer primary sources -- papers,
standards, official documentation -- over blogs and forum posts. Answer in
Italian, leaving titles and the names of techniques in the original. Where a
question has no answer in the literature, say so instead of filling the gap.

## What we already know (do not re-derive)

- `multipink` uses the Paul Kellet coefficients, identical to
  `no.pink_filter` in GRAME's `noises.lib`, documented there as *"original
  designed by invfreqz in Octave"* — an IIR fit made **in the z-plane**, so its
  corner frequencies are fractions of the sample rate. Doubling fs moves the
  whole −3 dB/oct region up an octave and the bottom falls off the fit.
  Measured on the filter's own magnitude response, against ideal pink and
  referred to 1 kHz: at 96 kHz it is **−3.09 dB at 20 Hz**, −2.27 at 25 Hz,
  −1.50 at 31.5 Hz, −0.82 at 40 Hz, and correct from 63 Hz up. At 44.1 and
  48 kHz the same deviations stay within 0.6 dB.
- GRAME's own documentation already proposes an alternative:
  `fi.spectral_tilt(N, f0, bw, alpha)` with `alpha = −0.5`, whose parameters
  are in **Hz**, and which is therefore sample-rate-correct by construction. It
  is a closed form using real exponentially spaced pole–zero pairs — J.O. Smith
  and H.F. Smith, *"Closed Form Fractional Integration and Differentiation via
  Real Exponentially Spaced Pole-Zero Pairs"*, arXiv:1606.06154 (2016). The
  `no.pink_noise` docs carry a `pink_noise_compare.dsp` comparing
  `pink_filter`, `spectral_tilt(3,...)` and `spectral_tilt(9,...)`.
- Voss–McCartney (`no.pink_noise_vm`) is a different family: a generator, not a
  shaping filter, so it cannot pink an arbitrary input.
- Our own measurement, which the literature rarely states: a **narrow
  low-frequency section in direct form collapses in single precision**. In a
  1/3-octave bank every band at or below 160 Hz landed on the epsilon floor at
  32 bits and was correct in double. Conditioning is a first-class criterion,
  not a footnote.

## The question

Which pinking filter should a **calibration-grade** pink generator use — one
whose output is the reference against which a loudspeaker is equalised, at any
sample rate from 44.1 to 192 kHz, over 20 Hz–20 kHz?

## What we need from the audit

1. **A map of the methods**, with citations: z-plane IIR fits (invfreqz,
   Prony, Steiglitz–McBride, Yule–Walker); closed-form pole–zero cascades
   (Smith & Smith and predecessors); analog-prototype-then-bilinear designs;
   fractional-integrator approximations of s^(−1/2); FFT/overlap-add spectral
   shaping; Voss–McCartney and its refinements (Trammell, McCartney's own
   corrections). For each: what it optimises, what it costs, what it assumes
   about the sample rate.
2. **Accuracy versus order**, stated in dB of deviation from −3.01 dB/octave
   over a named band. We need to know what order buys ±0.5 dB and what buys
   ±0.1 dB over 20 Hz–20 kHz.
3. **Behaviour at the band edges.** Every method degrades below its lower
   design limit; we care specifically about 20–40 Hz, which is where our defect
   showed up and where room correction matters most.
4. **Numerical conditioning.** Which topologies survive single precision at
   f/fs ≈ 4·10⁻⁴ (20 Hz at 48 kHz)? Cascaded first-order sections vs direct
   form vs state-variable/transposed forms.
5. **Phase and level.** Is minimum phase required for a measurement reference?
   How does the choice change the RMS attenuation, given that we must recompute
   a calibration constant afterwards (our `kCalibrationOffsetDb`)?
6. **Multi-stream use.** We run 64 mutually independent pink streams. Does any
   method impose shared state or correlate the streams?
7. **What the measurement standards say.** IEC 61260 (band filters) and any
   standard that specifies pink noise for loudspeaker/room measurement — is
   there a normative tolerance a calibration reference should meet?

## Deliverable

A comparison table of the candidate methods against criteria 2–7, a
recommendation with its reasoning, and — for the top two — enough of a design
procedure to implement them from the paper rather than from a code listing.
Prefer primary sources (papers, standards) over blog posts; where a widely
copied blog implementation is the de-facto standard (Kellet, Trammell), say so
explicitly and cite where its coefficients came from.

## Candidates already on our table

- **A — remap the existing fit.** Extract the poles and zeros of the current
  filter, invert the bilinear transform to recover an analog prototype, and
  re-discretise in `prepare(fs)`. At the original design rate the filter is
  identical to today's, so existing measurements stay comparable; at every
  other rate it becomes correct. Requires establishing the design rate
  empirically.
- **B — `fi.spectral_tilt(N, f0, bw, −0.5)`.** Correct by construction,
  arbitrary order, with a paper behind it. A genuinely different filter, so it
  breaks comparability with measurements already made.
- **C — a table of coefficient sets** pre-fitted per standard rate. Exact where
  it is tabulated, silent where it is not.

The audit should tell us whether these three are the right shortlist, and
whether the criteria above are the right ones to judge them by.
