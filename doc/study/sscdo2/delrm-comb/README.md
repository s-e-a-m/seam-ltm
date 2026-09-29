# delRM block 2: the comb

Part of the SSCDO#2 port (`logs/2026-09-29-sscdo2-ricognizione.md`), after `../delrm-delay/`.

delRM's channels 1 and 3 add the input to itself delayed: `_ <: de.delay(...), _ :> _` in the original.
The standard `fi.ff_comb(maxdel, M, b0, bM)` with both gains at 1 is the same filter, so `seam.tedesco.lib` calls it with DDELAY's prime delay:

```faust
delrmcomb(mt) = fi.ff_comb(1 << 15, sma.imt2npsamp(mt), 1, 1);
```

## The check
`./run.sh` builds `sdt.delrmcomb` and the original structure (with its swapped `de.delay` arguments) and runs `check.py`, at 48 and 96 kHz, mt = 7.291 m (Davide's initial 22 ms):

| | 48 kHz | 96 kHz |
|---|---|---|
| M = `sma.imt2npsamp(7.291)` | 1061 samples (22.10 ms) | 2113 samples (22.01 ms) |
| impulse response | 1 at 0 and at M, 0 elsewhere | same |
| against the original structure, 4 s of noise | identical | identical |
| mutation: original at M − 1 | caught (max diff 2.0) | caught |
| peak at 2·SR/M | +6.02 dB at 90.48 Hz | +6.02 dB at 90.87 Hz |
| notch at 1.5·SR/M | −284 dB at 67.86 Hz | −288 dB at 68.15 Hz |
| power on white noise | +3.00 dB | +3.01 dB |

The prime rounding moves the delay by a few samples from one rate to another, so the comb's spacing differs by 0.4 % between 48 and 96 kHz (45.24 against 45.43 Hz).
It is the price of incommensurable delays, and far below what the tuning by ear resolves.

Requirements as for `../lmo-bandfilter/` (faust, a C++17 compiler, the seam-ltm `.venv`, a faustlibraries clone in `NEW_LIBS`).

## On the contrabass clarinet
`render.py` runs the comb on track 1 (the microphone at 1 m) of `CCB_petalonio_oriz_DO.wav`: eight microphones around a contrabass clarinet on its low C, 96 kHz, 3.56 s of sound.
The note has a fundamental of 29.7 Hz and mainly odd partials (89, 148, 208, 267, 326, 385 Hz), the spectrum of a cylindrical bore closed at one end.

```bash
./run.sh                                          # builds the probes, runs the checks
../../../../.venv/bin/python render.py [SOURCE.wav]
```

It writes the dry note and the comb at 5, 7.291 and 10 m to `renders/`, normalised to −20 dBFS RMS, and in `renders/render-log.md` the comb's gain on each odd partial, measured against 2·|cos(π·f·M/SR)|.
They agree within 0.2 dB, except in the deepest notches, which the measurement reads shallower (−12.8 against −21.9 dB): a played partial is not a perfect line, and its spectral peak sits a little beside the notch.

The comb recolours the note partial by partial, and the distance chosen during the setup decides which partials it lifts:

| partial | 5.000 m | 7.291 m | 10.000 m |
|---|---|---|---|
| 29.6 Hz (1st) | −9.7 | −0.7 | +5.5 |
| 88.8 Hz (3rd) | −0.4 | +6.0 | +0.9 |
| 148.0 Hz (5th) | +3.3 | +2.8 | −17.3 |
| 207.6 Hz (7th) | +5.3 | −7.3 | +2.4 |
| 266.8 Hz (9th) | +6.0 | +5.4 | +5.9 |

At Davide's initial 7.291 m the comb reinforces the third partial and hollows the seventh; at 10 m it lifts the fundamental and removes the fifth.
Tuning delRM's delay is tuning a timbre, not only a time.

The WAVs carry a recorded performance and are kept local until its publication is agreed.
