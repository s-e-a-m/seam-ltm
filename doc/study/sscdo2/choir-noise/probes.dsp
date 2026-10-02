// probes.dsp -- the choir's noise: one stream per voice against one per band.
// The 16 noise bands of a voice, without the envelopes of the input, so that
// the noise alone is heard and measured. Channel c: f = 48, 48, 96, 96 Hz,
// a = 1, 1.01, 1.1, 0.9, Q = 350, as in the original's four instances.
import("stdfaust.lib");
sno = library("seam.noises.lib");
sdt = library("seam.tedesco.lib");
F(c) = ba.take(c+1, (48, 48, 96, 96));
A(c) = ba.take(c+1, (1, 1.01, 1.1, 0.9));
bands(c) = par(k, 16, fi.svf.bp(F(c) * pow(k+1, A(c)), 350));
// one stream per voice: block 3 of the SSCDO#2 noise, multinoise(12)
v4(c)  = sdt.choirnoise(4) : ba.selectn(4, c) <: bands(c) :> _;
// one stream per band: 64 streams after LMO's 8, multinoise(72)
v64(c) = sno.multinoiseblock(72, 8 + 16*c, 16) : bands(c) :> _;
// the original: every instance calls no.multinoise(16) from the same state,
// so the four channels carry the same 16 streams
vo(c) = no.multinoise(16) : bands(c) :> _;
vo_0 = vo(0); vo_1 = vo(1); vo_2 = vo(2); vo_3 = vo(3);
v4_0 = v4(0); v4_1 = v4(1); v4_2 = v4(2); v4_3 = v4(3);
v64_0 = v64(0); v64_1 = v64(1); v64_2 = v64(2); v64_3 = v64(3);
// the band filter alone, to check measure.py's model of fi.svf.bp
bp48 = fi.svf.bp(48, 350);
