// probe.dsp -- stunedrev, one line per process.
//   sq ph ex pi       SEAM, sdt.stline
//   osq oph oex opi   Davide Tedesco's stunedrev.dsp, verbatim but for the slider
//   dsq dph dex dpi   Davide's apf with SEAM's delays: isolates the structure
//   mut               sdt.stline's structure on line √2 with g = 0.7: must differ from dsq
// Build one with faust -pn <name>.
import("stdfaust.lib");
import("seam.lib");

ms = nentry("ms", 33, 1, 100, 1);
g = 1/sqrt(2);

sq = sdt.stline(sqrt(2), ms);
ph = sdt.stline((1+sqrt(5))/2, ms);
ex = sdt.stline(ma.E, ms);
pi = sdt.stline(ma.PI, ms);

// ---- Davide Tedesco, stunedrev.dsp (the version played in Pure Data)
np = ffunction(int dt_next_pr(int), "dt_nextprime.h", "");
apf(SRM,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(SRM*ma.SR,t-1),_)~(0-_) : mem+_;
t = ms/1000 : ba.sec2samp;
line(SRM, k) = seq(i, 42, apf(SRM, t*(i+1)*k : np, g));
osq = line(6, sqrt(2));
oph = line(7, (1+(sqrt(5)))/2);
oex = line(12, ma.E);
opi = line(14, ma.PI);

// ---- Davide's apf, SEAM's delays
dline(SRM, k) = seq(i, 42, apf(SRM, sdt.stdel(k, i, ms), g));
dsq = dline(6, sqrt(2));
dph = dline(7, (1+sqrt(5))/2);
dex = dline(12, ma.E);
dpi = dline(14, ma.PI);

mut = seq(i, 42, sjm.apfv(sdt.stmd(sqrt(2), i), sdt.stdel(sqrt(2), i, ms), 0.7));

process = sq;
