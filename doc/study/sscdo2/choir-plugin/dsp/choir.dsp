// sdt.choir(350, 1.5) on four generated inputs: channel c hears 16 sines at
// f_c*k, 0.05 each. The C++ test generates the same inputs by the same formula.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
src(c) = sum(k, 16, 0.05 * sin(2*ma.PI*sdt.choirf(c)*(k+1)*ba.time/ma.SR));
process = par(c, 4, src(c)) : sdt.choir(350, 1.5);
