// probe.dsp -- five forms of the all-pass, each as a chain of 42 sections
// with stunedrev's delays (line e, ms = 7: the start of the performance).
// Build one with faust -pn <name>.
import("stdfaust.lib");
import("seam.lib");

g = 1/sqrt(2);
k = ma.E;
ms = 7;
md(i) = sdt.stmd(k, i);
t(i)  = sdt.stdel(k, i, ms);
chain(apf) = seq(i, 42, apf(md(i), t(i)));

// SEAM, Moorer's form (seam.moorer.lib), the one stunedrev uses
moorer = chain(\(m,d).(sjm.apfv(m, d, g)));
// Davide Tedesco's apf (stunedrev.dsp), buffer passed directly
dav    = chain(\(m,d).(davapf(m, d, g)));
davapf(m,t,g,x) = (x+_ : *(-g) <: _+x,_ : de.delay(m,t-1),_)~(0-_) : mem+_;
// standard library, gain of opposite sign
std    = chain(\(m,d).(fi.allpass_comb(m, d, -g)));
// SEAM, Schroeder's form (seam.schroeder.lib)
sch    = chain(\(m,d).(sms.apfv(m, d, g)));
// Giuseppe Silvi's in-phi-rev (Canto alla durata), Schroeder's form written out
inphi  = chain(\(m,d).(ipapf(m, d, g)));
ipdflc(m,t,g) = (+ : de.delay(m, t-1))~*(g) : mem;
ipapf(m,t,g) = _ <: *(-g) + (ipdflc(m,t,g) : *(1-(g*g)));
// mutation: Moorer's form with the sign of g flipped; the checks must see it
mut    = chain(\(m,d).(sjm.apfv(m, d, -g)));

process = moorer;
