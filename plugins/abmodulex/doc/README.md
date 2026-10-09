# ABMODULEX

ABMODULEX converts the four capsules of a tetrahedral microphone (A-format) into first-order AmbiX (ACN, SN3D: A0 = W, A1 = Y, A2 = Z, A3 = X).
It is a pure matrix with no capsule-compensation filter, the front end of the TETRAREC chain, and the inverse of BAMODULEX.

## Parameters

| Parameter | Values | Default | What it does |
|---|---|---|---|
| Tetrahedron | LFU RFD RBU LBD / LFD RFU RBD LBU | LFU RFD RBU LBD | how the four input slots are read: the upright tetrahedron or its dual; a change crossfades Z over 25 ms |

The menu sits in the SETUP zone: it declares how the microphone is mounted.
A session saved before the menu existed opens on the upright tetrahedron, which is the ABMODULEX of before, bit for bit.

## The upright tetrahedron

```
A0 = (LFU + RFD + RBU + LBD) / 2     W
A1 = (LFU − RFD − RBU + LBD) / 2     Y
A2 = (LFU − RFD + RBU − LBD) / 2     Z
A3 = (LFU + RFD − RBU − LBD) / 2     X
```

The matrix is involutory (M² = I), so BAMODULEX uses the same coefficients and ABMODULEX followed by BAMODULEX is the identity: each capsule reaches the loudspeaker in its own position.

## The dual tetrahedron

Added on 2026-10-09 for the studio of *Studio sul Corpo d'Ombra #2*, where it is used first.
The dual reads the same four slots as LFD RFU RBD LBU: each capsule keeps its azimuth and takes the opposite elevation.
The dual is the upright tetrahedron mirrored in the horizontal plane, and the two tetrahedra together are the eight corners of a cube.
In AmbiX only the Z row changes sign; W, Y and X are the same.

Through BAMODULEX, which diffuses on the upright tetrahedron, the whole chain is one orthogonal matrix (rows: outputs LFU RFD RBU LBD; columns: input slots 1–4):

```
         ⎡  ½   ½  −½   ½ ⎤
out  =   ⎢  ½   ½   ½  −½ ⎥  · in
         ⎢ −½   ½   ½   ½ ⎥
         ⎣  ½  −½   ½   ½ ⎦
```

Every loudspeaker receives half of each capsule, one of them inverted: the capsule that holds, in the dual, the loudspeaker's azimuth at the opposite elevation.
The matrix keeps the energy, so the overall level does not change.

In the room, with TETRAREC feeding STONED, the dual leaves the omnidirectional mode (W) and the horizontal modes (X, Y) as they are and inverts the vertical mode.
Feedback that builds on the coupling between upper and lower drivers changes its sign, and the frequencies where it rings move; feedback in the other modes is untouched.
TETRAREC is a spaced ("exploded") tetrahedron, so the capsules differ in phase, and the half-sums comb-filter each output in a different way: the dual also changes the timbre of a single pass.

The same result comes from a chain with no menu: ABMODULEX, the polarity of channel 3 (A2, Z) inverted, BAMODULEX.
That is how the idea was first tried in the room.

The other way to reach the dual tetrahedron, a rotation of 90° in azimuth (LFU → LBU, RFD → LFD, RBU → RFU, LBD → RBD), turns X and Y and leaves Z.
It is available without this menu as ABMODULEX → XYPRROT (yaw 90°) → BAMODULEX.

## Specification

`sam.abmodulexw(dual)` of `seam.ambisonics.lib` (faust-libraries), with `sam.abmodulex = sam.abmodulexw(0)`; the C++ that faust generates from `sam.abmodulex` is byte-identical before and after the change.
`doc/abmodulex.dsp` points to it with `dual` as an entry.

## Verification

| check | result |
|---|---|
| the upright tetrahedron against `encode()` | identical to the bit |
| capsules in slots 1 and 3 on the dual, against literal values | exact |
| Z from +1 to −1 in 25 ms, at 96 and 48 kHz, W Y X untouched | linear to 1e-12, W Y X identical to the bit |
| the dual through BAMODULEX against the matrix above | exact |
| an empty state (a session saved before the menu) | the upright tetrahedron |

Every test was verified by mutation, eleven mutations in all, every one RED: the gain on the Y row, on X instead of Z, on W with Z, a switch that jumps, a 50 ms glide, the glide counted at 96 kHz whatever the rate, Z starting at 0, the dual as default, a short state leaving the box as it was, the threshold above 0.5, NaN read as the dual.
