// probes.dsp -- the LMO with two beating oscillators, and its checks.
//
// Davide Tedesco's intention for the LMO's second oscillator (2026-09-29):
// two bands that can beat; at 0 Hz apart they share the frequency and are
// decorrelated; at a distance d each moves d/2 from the reference centre.
//
//   lmo2    the design: 2N streams from ONE no.multinoise(2N) call; streams
//           0..N-1 feed the band at f - d/2 + k, streams N..2N-1 the band at
//           f + d/2 + k; channel k sums the two, scaled by 1/sqrt(2) because
//           two independent noises add in power.
//   osc     the 2N bands before the sum (ch 0..7: A0..A3, B0..B3), to check
//           that A_k and B_k are independent.
//   one     sdt.lmoosc(4, f): the single oscillator, the level reference.
//   lib     sdt.lmo(4, f, d) from seam.tedesco.lib: must equal lmo2 sample for sample.
//   twocall MUTATION for the self-test: the two oscillators from two separate
//           no.multinoise(N) calls, which the fixed seed makes identical.
//
// Controls (set by analyze.py through the harness):
//   f     band centre of channel 0, Hz (97.44 = slider 96 x 1.015, cue 1)
//   d     distance between the two bands, Hz
//   mode  0: d fixed; 1: d ramps linearly from 0 to d1 over dur seconds
//   ch    output channel

import("seam.lib");

f    = hslider("f", 97.44, 20, 2000, 0.0001);
d0   = hslider("d", 0, 0, 200, 0.0001);
mode = hslider("mode", 0, 0, 1, 1);
d1   = hslider("d1", 40, 0, 200, 0.0001);
dur  = hslider("dur", 60, 0.01, 1000, 0.0001);
ch   = hslider("ch", 0, 0, 7, 1);

d = select2(mode, d0, d1 * min(1, ba.time / ma.SR / dur));

bandsA(N, f, d) = par(i, N, sdt.lmoband(max(1, f - d/2 + i)));
bandsB(N, f, d) = par(i, N, sdt.lmoband(f + d/2 + i));

lmo2(N, f, d) = no.multinoise(2*N) : bandsA(N, f, d), bandsB(N, f, d)
             :> par(i, N, /(sqrt(2)) : *(sdt.lmodens));

lmo2_ch    = lmo2(4, f, d) : ba.selectn(4, ch);
osc_ch     = no.multinoise(8) : bandsA(4, f, d), bandsB(4, f, d) : ba.selectn(8, ch);
one_ch     = sdt.lmoosc(4, f) : ba.selectn(4, ch);
lib_ch     = sdt.lmo(4, f, d) : ba.selectn(4, ch);
twocall_ch = (no.multinoise(4) : bandsA(4, f, d)), (no.multinoise(4) : bandsB(4, f, d))
           :> par(i, 4, /(sqrt(2))) : ba.selectn(4, ch);

process = lmo2_ch;
