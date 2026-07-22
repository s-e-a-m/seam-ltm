# lr2xhgr — future work

Notes recorded at the host check of 2026-07-22.

## Shared harmonic gain trims (2026-07-22)

lr2xhgr needs the same `A1`, `A2`, `A3` gain trims that
[`plugins/m2xhgr/doc/roadmap.md`](../../m2xhgr/doc/roadmap.md) describes,
with one difference that decides the whole design: **one trim drives both
channels.**

### What is missing

Three trims, ±12 dB each, on the first-order components `A1`, `A2` and `A3`,
with `A0` excluded for the same reason as in m2xhgr.

lr2xhgr runs two Haar banks, one per input channel, so a naive port would
produce six controls — an `A1` for the left bank and an `A1` for the right,
and so on.
It produces three instead.
The `A1` control scales the `A1` output of *both* banks, and likewise `A2`
and `A3`.

### Why it matters

The size of the encoded image is a property of the encoding, not of one side
of it.
A trim pair that let the left and right banks differ would tilt the image
sideways, and lr2xhgr already has a control for placing the two channels
against each other: Divergence.
Two mechanisms acting on the same perceptual quantity would leave the user
unable to say which one produced the result they hear.

Sharing the trim keeps the two controls separate in meaning: Divergence says
how far apart the two encodings sit, the trims say how large both of them
are.

### Shape the answer probably takes

Three `RangeParameter`s in dB (−12 … +12, default 0), continuing the
existing block (`kParamDivergence` = 99, `kParamYaw` = 100 …
`kParamRoll` = 102, so 103, 104, 105).
Each value converts once to a linear gain and is applied to output channels
1, 2 and 3 of *both* `HaarState` banks, before the divergence rotation —
rotation mixes the first-order components among themselves, so a trim placed
after it stops naming a component.

Seven fine controls (Divergence, Yaw, Pitch, Roll, and three trims) puts the
window firmly in the L format of
[`doc/style/ui-style.md`](../../../doc/style/ui-style.md), alongside dslar
and ltglide.

`seam.ambisonics.lib` wants the matching change, since `lr2xhgr` there is
defined in terms of `m2xhgr`: adding the gain stage to the mono definition
carries it into the stereo one, and the sharing rule is then a property of
how lr2xhgr passes its arguments.

## A second topology: LR → MS → X (2026-07-22)

A future variant could encode by way of mid/side rather than encoding the
two channels directly.

Today lr2xhgr Haar-decomposes L and R separately and places the two
resulting fields with a divergence angle.
The alternative first matrixes the input to mid and side — the operation
SDMX already implements — and encodes from there.

This is recorded as a direction, not a specification.
It stays open until someone works out what the mid/side path does to the
soundfield that the direct path does not, and whether the result deserves a
separate plugin or a topology switch inside this one.
