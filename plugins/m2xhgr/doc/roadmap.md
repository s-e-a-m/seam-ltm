# m2xhgr — future work

Notes recorded at the host check of 2026-07-22.

## Harmonic gain trims — the G the name already promises (2026-07-22)

The name reads M(ono) 2 X(AmbiX) H(aar) G(ain) R(otation).
The window today offers the R and nothing else: Yaw, Pitch, Roll.
The G is the missing letter, and this note describes what it is.

### What is missing

One gain trim per spherical-harmonic channel, spanning ±12 dB, on `A1`, `A2`
and `A3`.
`A0` is deliberately excluded.

`A0` is the omnidirectional component, and it is the reference the other
three are weighed against.
Trimming it would move the level of the whole encoding, which the host's own
fader already does.
Holding `A0` fixed is what makes the three trims read as a ratio rather than
as three more volume controls.

### Why it matters

The trims control the *size* of the conversion.
Scaling the first-order components against the omnidirectional one changes
how wide the encoded image sits: a small `A1`/`A2`/`A3` relative to `A0`
concentrates the source towards a point, a large one opens it out across the
soundfield.
That is a musical decision about the encoding, not a correction applied to
it, which is why the controls belong in the plugin window next to the
rotation rather than in a calibration path.

The same reasoning makes the trims worth having even when the encoding is
otherwise correct: the Haar bank fixes one relationship between `A0` and the
first-order components, and the trims are how a composer disagrees with it.

### Shape the answer probably takes

Three `RangeParameter`s in dB, range −12 to +12, default 0, continuing the
existing parameter block (`kParamYaw` = 100 … `kParamRoll` = 102, so 103,
104, 105).
Each is converted to a linear gain and applied to the Haar bank's outputs
1, 2 and 3.

The gains apply **before** `rotateYPR`.
Rotation mixes `A1`, `A2` and `A3` among themselves, so a trim placed after
it would no longer correspond to the named component the user thinks they
are adjusting.
Applied before, "the `A1` trim" means the same thing at every rotation
angle.

Six fine controls crosses the S → L threshold of
[`doc/style/ui-style.md`](../../../doc/style/ui-style.md), so m2xhgr becomes
an L window: rotation in one column, gains in the other.

The Faust side wants the same addition in `seam.ambisonics.lib`, so that the
specification still describes the plugin — `m2xhgr` there is currently
`sdw.haarmn(1)` followed by `rotateYPR`, with no gain stage between them.

The stereo sibling wants the same trims with a different sharing rule; see
[`plugins/lr2xhgr/doc/roadmap.md`](../../lr2xhgr/doc/roadmap.md).
