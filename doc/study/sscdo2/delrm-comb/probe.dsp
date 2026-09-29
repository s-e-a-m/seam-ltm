// probe.dsp -- sdt.delrmcomb against the original delRM comb structure.
//   lib   sdt.delrmcomb(mt), from seam.tedesco.lib
//   orig  Davide's line, `_ <: de.delay(max, del), _ :> _` with his swapped arguments,
//         the delay set to M samples (the value sma.imt2npsamp gives for mt)
import("seam.lib");
mt = hslider("mt", 7.291, 0, 30, 0.001);
M  = hslider("M", 1061, 0, 32767, 1);
lib  = sdt.delrmcomb(mt);
orig = _ <: de.delay(M, 384000), _ :> _;
process = lib;
