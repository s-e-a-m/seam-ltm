# BASTOOGE

BASTOOGE is the decoder that belongs to one loudspeaker body, the S.T.OOGE mk2g: a tetrahedral guitar loudspeaker with four 8-inch cones in a common bass-reflex chamber.
S.T.OOGE is developed with Francesco Palmieri, guitarist.
BASTOOGE is `bamodulex` (Gerzon's BA-module, AmbiX to LFU, RFD, RBU, LBD) with the two filters that a per-face equaliser cannot express: one on W, one shared by X, Y and Z, both ahead of the matrix.
Its Faust specification is `stt.bastooge` in `faust-libraries/src/seam.st.lib`; the C++ is written by hand from it, as everywhere in the suite.

## Why a decoder and not the amplifier

The amplifier carries one filter set, identical on the four faces.
An identical filter on every face commutes with the orthogonal matrix, so it acts identically on W, X, Y and Z.
In a common chamber W and the dipoles are different physical objects.
Only W compresses the air and drives the ports; in a dipole mode the summed cone volume is zero, the internal pressure is near zero, and the cones work unloaded, without the reflex.
The dipole therefore radiates less than W at low frequency, and no setting common to the four faces can correct that.
The division of labour is this.
BASTOOGE carries the constants of the model: the W/dipole balance and the dipole cut, the same for every mk2g.
The amplifier carries what is common to the four modes and specific to the unit, the room and the concert, and it is calibrated with `strx` after BASTOOGE is in the chain.

## Signal flow

```
AmbiX in: A0 (W), A1 (Y), A2 (Z), A3 (X)
  |
  +- test selector (Normal / W / X / Y / Z)
  |
  +- W:        bell(fc) -> all-pass(fc)
  +- Y, Z, X:  high-pass LR4(fc), the same on the three
  |
  +- bamodulex matrix -> LFU, RFD, RBU, LBD
```

The bypass removes the bell, the all-pass and the high-passes, and keeps the matrix and the test selector.
With bypass on, BASTOOGE is `bamodulex`, and the test selector gives the bare modes.
The filters keep running while bypassed and only the output is chosen, as `select2` does in the Faust, so that switching bypass off continues from live filter state rather than from state frozen when bypass was engaged.
The switch itself, between dry and filtered and between test modes, is instantaneous, as `select2` is: a step in the signal is heard.
The cut is followed by a one-pole of 20 ms, so that moving the slider does not click.
Nothing depends on a fixed sample rate: the filters are designed at the host rate.

## Parameters

| Parameter | Range | Default | Note |
|---|---|---|---|
| Dipole cut fc | 100-250 Hz, logarithmic | 160 Hz | moves the high-pass, the all-pass and the bell together |
| Test | Normal, W, X, Y, Z | Normal | saved with the state like any parameter; the footer names a test mode in capitals |
| Bypass | off / on | off | filters out, matrix in |

Below 100 Hz the dipoles are 11-14 dB under W and the cones work unloaded; above 250 Hz the dipoles already reach W within a few dB.
A higher cut gives more headroom and less excursion, for loud playing; a lower cut lets the directivity reach lower, for quiet playing.
The footer reads three rows: `W BELL <f0> Hz, Q <q>, <gain> dB` (the bell the cut selects, shown also while bypassed), `CUT <fc> Hz`, and a status line.
The status line warns when a dipole test runs with bypass on, because the cones then take the bass unloaded.

## The dipole/W ratio

The measurements were taken on 2026-11-18 at LEAP (Rome), at one position: the frontal AB pair at 1 m, about the critical distance of the 83 m3 room.
The ratio is the power mean of the three axes, (X lobe + 2 X null) / 3, over W, in third-octave bands.
It is modelled as the radiation law of a compact source:

```
r(f) = x^2 / (1 + x^2),   x = f / f1,   f1 = 359.3 Hz
```

The dipole radiates (ka)^2 times the monopole while ka is small and as much as the monopole once ka is large.
f1 is where the dipole is 3 dB under W; read as ka = 1, it puts the effective radius of the radiating body at about 15 cm.
The fit is by least squares in dB on the third-octave centres from 200 Hz to 6.3 kHz, with RMS error 1.16 dB.
The band starts at 200 Hz to leave out the reflex hump of W at 160 Hz, which belongs to W alone and therefore to the amplifier.
Above 400 Hz the measurement carries about 2 dB of single-position room ripple, which the model does not follow either.
Below 200 Hz the model extrapolates the (ka)^2 law (-7.9 dB at 160 Hz against a measured -13.8 dB); the difference is the W hump.

The measured values, with the model for comparison:

| Centre (Hz) | Measured (dB) | Model (dB) |
|---:|---:|---:|
| 19.95 | -3.0 | -25.1 |
| 25.12 | -0.3 | -23.1 |
| 31.62 | -1.9 | -21.1 |
| 39.81 | -5.4 | -19.2 |
| 50.12 | -3.9 | -17.2 |
| 63.10 | -10.6 | -15.2 |
| 79.43 | -12.3 | -13.3 |
| 100.00 | -13.4 | -11.4 |
| 125.89 | -13.3 | -9.6 |
| 158.49 | -13.8 | -7.9 |
| 199.53 | -7.2 | -6.3 |
| 251.19 | -5.5 | -4.8 |
| 316.23 | -3.0 | -3.6 |
| 398.11 | -2.4 | -2.6 |
| 501.19 | -0.9 | -1.8 |
| 630.96 | -0.1 | -1.2 |
| 794.33 | -0.8 | -0.8 |
| 1000.00 | -3.1 | -0.5 |
| 1258.93 | -2.0 | -0.3 |
| 1584.89 | -0.5 | -0.2 |
| 1995.26 | -1.8 | -0.1 |
| 2511.89 | -1.6 | -0.1 |
| 3162.28 | -0.6 | -0.1 |
| 3981.07 | +0.2 | -0.0 |
| 5011.87 | +0.7 | -0.0 |
| 6309.57 | -0.1 | -0.0 |
| 7943.28 | +0.4 | -0.0 |
| 10000.00 | +0.2 | -0.0 |
| 12589.25 | +0.0 | -0.0 |
| 15848.93 | -0.6 | -0.0 |
| 19952.62 | -0.4 | -0.0 |

Provenance.
The values come from the S.T.OOGE mk2g session of 2026-11-18 in `dream-stone`, a private repository, so they are reproduced here and the public repository stands on its own.
The files are `stooge/mk2g/misure/2026-11-18-184027-stone1-W.txt`, `stooge/mk2g/misure/2026-11-18-184624-stone1-X.txt` and `stooge/mk2g/misure/2026-11-18-185200-stone1-X-ruotato.txt` (X with the body rotated by 90 degrees, microphones fixed).
The analysis is in `logs/2026-11-18-stooge-mk2g-primo-ruggito.md`, and the fitted table in `data/stooge-mk2g-bastooge.dat`.
The table above is `stone.bastooge.rapporto_misurato` run on that directory.

## The W bell

Let r(f) be the model above and |H(f; fc)|^2 the power response of the dipole high-pass.
The target attenuation of W, in power, is

```
T_W(f; fc) = 1 - |H(f; fc)|^2 * (1 - min(r(f), 1))
```

Where the high-pass lets the dipoles through, T_W = r and W comes down to the level the dipoles reach.
Where the high-pass has removed them, T_W = 1 and W is left alone.
In between the transition follows the high-pass itself.
For each fc on a grid, one peaking biquad (f0, Q, gain) is fitted to T_W by least squares in dB on a logarithmic grid from fc/2 to 6.3 kHz.
The table has 16 rows at fc = 100 * 2.5^(k/15), k = 0..15; between rows the plugin interpolates f0 geometrically and Q and the gain linearly.
It lives in `plugins/_common/seam_st.h` and in `seam.st.lib` (`stt.mk2g_bell`), where the constants sit in one isolated table so that a second body can have its own.

| k | fc (Hz) | f0 (Hz) | Q | Gain (dB) |
|---:|---:|---:|---:|---:|
| 0 | 100.0 | 222.3 | 0.837 | -4.98 |
| 1 | 106.3 | 231.8 | 0.847 | -4.72 |
| 2 | 113.0 | 241.8 | 0.858 | -4.46 |
| 3 | 120.1 | 252.4 | 0.868 | -4.21 |
| 4 | 127.7 | 263.5 | 0.879 | -3.97 |
| 5 | 135.7 | 275.1 | 0.888 | -3.73 |
| 6 | 144.3 | 287.4 | 0.898 | -3.50 |
| 7 | 153.4 | 300.3 | 0.907 | -3.28 |
| 8 | 163.0 | 313.9 | 0.916 | -3.07 |
| 9 | 173.3 | 328.3 | 0.924 | -2.86 |
| 10 | 184.2 | 343.5 | 0.931 | -2.67 |
| 11 | 195.8 | 359.5 | 0.939 | -2.48 |
| 12 | 208.1 | 376.4 | 0.945 | -2.30 |
| 13 | 221.2 | 394.4 | 0.952 | -2.13 |
| 14 | 235.2 | 413.3 | 0.957 | -1.96 |
| 15 | 250.0 | 433.4 | 0.962 | -1.81 |

The bell is gentler and higher than the one first tried in the room (250 Hz, Q 1.4, -5 dB).
That one was derived with the dipoles fully present, so at 250 Hz it cut W by 5 dB where the high-pass still held the dipoles 1.3 dB down, and the measurement showed a residual dip of about 1.5 dB.

## The dipole high-pass and the W all-pass

The dipoles pass through a fourth-order Linkwitz-Riley high-pass: two cascaded second-order Butterworth sections (Q = 1/sqrt 2) at fc, the same on Y, Z and X.
One filter serves the three dipoles because, in the tetrahedral symmetry, they span an irreducible representation of the rotation group, and by Schur's lemma the body acts on them as a scalar.
The measurement agrees: X and Y, taken with the body rotated and the microphones fixed, coincide within about 1 dB.
W goes through a second-order all-pass at fc, Q = 1/sqrt 2.
The Linkwitz-Riley identity HP_LR4 + LP_LR4 = AP2 means the phase of that high-pass is exactly the phase of the all-pass, so W and the dipoles keep matched phase at every frequency.
It matters for coherent sources: `lr2xhgr` puts the same signal on W and X, and at the microphone their pressures add in amplitude.
In the room, adding the all-pass on W raised the band at 250 Hz by 2.2 dB with an `lr2xhgr` source.

## The test selector

In a test mode, input channel 1 (A0) feeds the chosen mode alone, with gain 2, and the other three inputs are ignored.
The gain of 2 compensates the 1/2 of the matrix, so each face receives the input amplitude in every test mode: the same voltage per face, the condition of the measurement protocol.
The signs, in the order LFU, RFD, RBU, LBD, are W + + + +, X + + - -, Y + - - +, Z + - + -.
Test modes go through the decoder filters unless bypass is on.
A mono `multipink` stream into channel 1 then gives, with one menu, the W, X, Y and Z measurements, decoded or bare.

## Measuring with multipink and strx

1. Put `multipink` (one mono stream) on a track, then BASTOOGE, then the interface outputs.
2. Set Test to W, X, Y or Z in turn, with the cut at the value you intend to play with.
3. Measure each mode with `strx` at the listening position.
4. Calibrate the amplifier on what the four modes share: the cone roll-off, the reflex hump at 160 Hz, the bass extension.
5. For the bare modes, turn bypass on and repeat.
   Keep the level low with a dipole test in bypass: the cones then take the bass unloaded.

## Verification

`tools/faust-bastooge-ab.sh` compares the C++ against `seam.st.lib` compiled by Faust, with a gate of 1e-12.
The two chains take 28 cases at 48 and 96 kHz, all 16 rows of the table and the clamp, worst difference 1.1e-16.
The whole decoder takes 64 cases at 160 Hz and 48 kHz, every test mode with and without bypass on every face, worst difference 6.1e-17.
With bypass on and test Normal, the output equals `bamodulex` sample for sample.

## Open points

- **The phase of the bell.**
  The bell is minimum-phase and only W has it; near 250 Hz it leaves a few degrees of mismatch between W and the dipoles.
  The measurements do not show a problem yet; a later version can give the dipoles a matching all-pass.
- **One position, one room.**
  The ratio comes from the frontal pair at 1 m in the LEAP stage area; a second position or room would confirm f1.
- **The W hump at 160 Hz** is left to the amplifier by construction; at low cut settings the amplifier calibration has to account for it.
- **Z was not measured.**
  By symmetry it equals X and Y; the floor is room, not body.
- **Validation in the room** is still to do: repeat the `lr2xhgr` measurement with BASTOOGE in place of the equaliser chain, and expect the residual dip at 200-250 Hz within about 1 dB of the power prediction, at fc 160 Hz and at the ends of the range.
- **Other bodies.**
  A model menu (S.T.ONE, STONED, S.T.ON3L) waits until a second body has been measured the same way.
