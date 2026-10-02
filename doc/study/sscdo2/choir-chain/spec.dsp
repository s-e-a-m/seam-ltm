// spec.dsp -- sdt.choir against the original, transcribed, on the same noise.
// Input: the channel's own signal (spec.py writes 16 sines at f·k).
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
C = 0;                                   // channel under test, set by -pn variants below
F(c) = sdt.choirf(c); A(c) = sdt.choira(c);
sel(c) = route(64, 16, par(k, 16, (16*c + k + 1, k + 1)));   // channel c's streams
nz(c) = sdt.choirnoise(4) : sel(c);
// the original, pitchDetectorChoirMcAdams.dsp, with its multinoise replaced by
// the same streams and its final fi.dcblocker left out
svfBandPass(f, qu) = fi.svf.bp(f, qu);
multi_bandpass(f, qu, release) = par(i, 16, svfBandPass(f * (i + 1), qu) : an.amp_follower(release));
multi_bandpass_noise(f, a, qu) = par(i, 16, svfBandPass(f * (i + 1)^a, qu));
multiplier(N) = ro.interleave(N, 2) : par(i, N, *);
orig(c) = (_ <: multi_bandpass(F(c), 350, 1.5)), (nz(c) : multi_bandpass_noise(F(c), A(c), 350))
        : multiplier(16) :> _/(350*(2*ma.PI));
ours(c) = _, nz(c) : sdt.choirchan(F(c), A(c), 350, 1.5);
// the four-channel sdt.choir with the input on channel c only
full(c) = _ <: par(i, 4, *(i == c)) : sdt.choir(350, 1.5) : ba.selectn(4, c);
// differences, computed in double inside the DSP
d_orig(c) = _ <: ours(c), orig(c) : (_, *(sdt.choirdens)) : -;
d_full(c) = _ <: ours(c), full(c) : -;
o0 = orig(0); o1 = orig(1); o2 = orig(2); o3 = orig(3);
do0 = d_orig(0); do1 = d_orig(1); do2 = d_orig(2); do3 = d_orig(3);
df0 = d_full(0); df1 = d_full(1); df2 = d_full(2); df3 = d_full(3);
ours0 = ours(0);
// above Nyquist: f = 5000, bands from k = 4 at 20 kHz and up
nyq_ours = _, nz(0) : sdt.choirchan(5000, 1, 350, 1.5);
nyq_orig = (_ <: multi_bandpass(5000, 350, 1.5)), (nz(0) : multi_bandpass_noise(5000, 1, 350)) : multiplier(16) :> _/(350*(2*ma.PI));
