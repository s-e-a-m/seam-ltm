# STUNEDREV

STUNEDREV is the APF of *Studio sul Corpo d'Ombra #2* (Alice Cortegiani, Davide Tedesco), ported from the Pure Data patch for Giuseppe Silvi's performance.
It is four independent lines of 42 all-pass sections in series, one line per face of STONED, each tuned by an irrational ratio: √2, φ, e, π.
It is a long memory more than a reverberation: a section returns all the energy it receives, delayed on average by its own time, so the energy of a line returns on average after the sum of its 42 delays, 106, 69, 17 and 201 s at the starting times.
The direct path crosses 42 gains of −1/√2 and arrives at −126 dB: a sound entering STUNEDREV is almost silent at first and comes back over seconds to minutes, on the time scale of each face.
Inputs and outputs 1–4 are the lines √2, φ, e, π, in the order of the patch.

## Parameters

| Parameter | Range | Default | What it does |
|---|---|---|---|
| POWER | off / on | off | fades the output in or out over 25 ms; the lines keep running |
| RESET | button | — | empties the four memories (see below) |
| t √2, t φ, t e, t π | 1–100 ms, step 1 | 83, 47, 7, 71 ms | the time of each line: section i delays by t·(i+1)·k |
| input | 0–1 | 0 | linear gain into the lines, CC83 in the original, 25 ms ramp |
| output | 0–1 | 0 | linear gain out of the lines, CC84 in the original, 25 ms ramp |

The starting times are those of the Pd patch; 33 ms is only the default of the original `.dsp` sliders.
The footer shows the energy centroid of each line, the sum of its 42 delays: how long each face remembers, recomputed when a time moves.
Below it, the memory the plugin holds and its status: ready, clearing, or allocation failed.

### Moving a time
The 42 delays of the line jump to their new primes at the next audio block, as in the specification and in the original.
Each buffer holds the whole history up to the longest delay the slider can ask, so a jump reads older or newer sound: a click, never garbage.
The times are settings: no cue of the piece moves them.

### RESET
RESET fades the output out over 25 ms, zeroes the memory a slice per audio block while the output stays silent and the input is ignored, and fades back in.
At 96 kHz it lasts 0.42 s, the same at any block size; a second click during the clearing restarts it from the beginning.
RESET serves rehearsals: it is not a parameter, no automation can trigger it, and no preset stores it.
A momentary parameter would be lost in a host that merges the press and the release into one point (ltglide's experience in Reaper), so the button reaches the engine directly.

## Sample rate

STUNEDREV sounds as it does at 96 kHz, the rate SSCDO#2 is played at, whatever the session's rate.
The delays are milliseconds converted at the session's rate, rounded to the nearest sample and moved to the next prime strictly above, so every time, and every memory, is the 96 kHz one to within one prime gap per section.
The primes themselves differ from rate to rate: a prime is prime at one rate only, and each rate has its own incommensurable set, a feature of the SEAM system.

## Memory

Each of the 168 sections has a buffer sized exactly for its longest delay, in one block allocated and zeroed when the host activates the plugin, never in the audio thread: 588 MiB at 96 kHz, 1.15 GiB at 192 kHz.
The specification (`sdt.stmd`) adds a fixed margin of 150 samples instead, because Faust sizes a buffer when it compiles and cannot compute a prime there; the exact size is correct at any rate, where the fixed margin would stop covering the gaps between primes at 384 kHz.
The sound is identical; the original Pd external held 15.2 GiB.
If the memory cannot be allocated, the plugin stays silent and the footer says "allocation failed"; the host keeps running.

## Specification

The DSP is `sdt.stunedrev(t1, t2, t3, t4)` of `seam.tedesco.lib` (faust-libraries), written again by hand in `source/stunedrev_dsp.h`:

```
apfv(md,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(md,t-1),_)~(0-_) : mem+_;   // seam.moorer.lib
ms2npsamp(ms)  = select2(n < 2, n : sff.np, n)
                 with { n = int(floor(ms*ma.SR/1000 + 0.5)); };               // seam.math.lib
stdel(k,i,ms)  = sma.ms2npsamp(ms*(i+1)*k);
stline(k,ms)   = seq(i, 42, sjm.apfv(stmd(k,i), stdel(k,i,ms), 1/sqrt(2)));
stunedrev(t1,t2,t3,t4) = stline(sqrt(2),t1), stline((1+sqrt(5))/2,t2),
                         stline(ma.E,t3), stline(ma.PI,t4);
```

The engine wires two libraries of `plugins/_common/` written for reuse: `seam_moorer.h` (Moorer's all-pass, `sjm.apfv`, with g and the buffer as parameters) and `seam_primes.h` (a sieve behind `sff.np`, so a change of time needs no division in the audio thread).

`tests/stunedrev_dsp_test.cpp` checks the engine against the specification: the 16 800 delays (100 times, 42 sections, 4 lines) equal `sdt.stdel` exactly at 96 and 48 kHz; the four lines agree with `sdt.stunedrev(83, 47, 7, 71)` over 30 s to 1.8e-15 of the peak at 96 kHz and 1.6e-15 at 48 kHz, and to 8.7e-16 across a change of time.
Every test was verified by mutation; the references, the record of the mutations and the measurement of the cost are in `doc/study/sscdo2/stunedrev-plugin/`.
The cost is 9.4 % of a core at 96 kHz and 18.8 % at 192 kHz (Intel i7-8850H, 2.6 GHz).

## Out of scope

In the Pd patch an `adc~ 5 6 7 8` also feeds the reverb directly, beside the APF INPUT fader; whether that is intended is a question for Davide, and the plugin has one input, governed by `input`.
A change of time is not smoothed: a crossfade would break the all-pass during the fade and depart from the specification.
The cues and the MIDI faders live in Reaper, not in the plugin.
