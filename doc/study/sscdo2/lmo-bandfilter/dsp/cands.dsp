// cands.dsp -- the LMO band-filter candidates, one process name each.
//
// Build one candidate with `faust -pn <name>` (see ../build.sh).
// Every candidate is mono: one noise input, one filtered output, centred on F
// from ctl.lib (F = 1.015 * slider, as in Davide Tedesco's LMO).
// C, C3 and C2 are scaled by 0.5 so its passband sits at -6.02 dB, the peak of Davide's
// HP24.LP24 product; their tops are maximally flat, A's is rounded.
//
//   hplp    A/B  fi.highpass(24,F) : fi.lowpass(24,F-0.0001)   (Davide's filter)
//                built against the OLD libraries it is A (direct-form tf2s biquads),
//                against the NEW libraries it is B (TPT SVF sections, faustlibraries 0965ea2)
//   davide  A    hplp, doubled (two identical oscillators summed) and DC-blocked:
//                Davide's true output level at LMO Volume = 1
//   bp24    C    fi.bandpass(24, F/rl, F*ru)  -- Butterworth, Nh = 24 = half the order (48):
//                24 SVF second-order sections (12 tf2sb, SVF since 4b251bf), as many as
//                HP24:LP24 (12+12), and a comparable skirt order
//   bp3     C3   fi.bandpass(3, F/rl, F*ru)  -- the same design at Nh = 3 (3 sections)
//   bp2     C2   fi.bandpass(2, F/rl, F*ru)  -- the same design at Nh = 2 (2 sections)
//                C3 and C2 are the orders whose skirts come nearest A's within a quarter
//                octave (analytic, see results.md, "No order of fi.bandpass reproduces A");
//                rendered so the reasoning can be heard, not only read.
//   noise0       no input: stream 0 of no.multinoise(8), the source of every render
//
// rl, ru are set at run time by analyze.py from A's measured -3 dB edges.
// lpd is Davide's -0.0001 Hz LP offset; exposed only so the self-test can
// deliberately mistune the filter.

import("stdfaust.lib");
import("ctl.lib");

lpd = hslider("lpd", -0.0001, -100, 100, 0.0001);
rl  = hslider("rl", 1.0374, 1, 4, 0.000001);
ru  = hslider("ru", 1.0374, 1, 4, 0.000001);

hplp   = fi.highpass(24, F) : fi.lowpass(24, F + lpd);
davide = hplp <: _, _ :> fi.dcblockerat(20);
bp24   = fi.bandpass(24, F / rl, F * ru) * 0.5;
bp3    = fi.bandpass(3, F / rl, F * ru) * 0.5;
bp2    = fi.bandpass(2, F / rl, F * ru) * 0.5;

// Stream 0 of no.multinoise(8): the noise that feeds Davide's channel k=0.
noise0 = no.multinoise(8) : _, par(i, 7, !);

process = hplp;
