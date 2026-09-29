// probe.dsp -- stunedrev's 16 800 delays, one per output sample.
// Sample n encodes the line j = n / 4200, the section i = (n / 100) % 42 and
// the time ms = n % 100 + 1. Build with faust -pn seam or -pn dav.
import("stdfaust.lib");
import("seam.lib");

n  = ba.time;
j  = int(n / 4200);
i  = int(n / 100) % 42;
ms = n % 100 + 1;
k  = ba.selectn(4, j, sqrt(2), (1+sqrt(5))/2, ma.E, ma.PI);

// SEAM: sdt.stdel, rounding then sff.np
seam = sdt.stdel(k, i, ms);

// Davide Tedesco's stunedrev.dsp: samples from the slider, times (i+1)·k,
// truncated by the int argument of his next_pr
dnp = ffunction(int dt_next_pr(int), "dt_nextprime.h", "");
dav = (ms/1000 : ba.sec2samp) * (i+1) * k : dnp;

process = seam;
