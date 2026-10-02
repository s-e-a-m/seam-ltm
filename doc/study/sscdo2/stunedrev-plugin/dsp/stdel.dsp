// The 168 delays of sdt.stdel at the time of the "ms" entry, line by line.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
ms = nentry("ms", 1, 1, 100, 1);
line(k) = par(i, 42, sdt.stdel(k, i, ms));
process = line(sqrt(2)), line((1+sqrt(5))/2), line(ma.E), line(ma.PI);
