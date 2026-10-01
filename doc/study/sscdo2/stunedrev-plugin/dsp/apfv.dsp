// One section, Moorer's form: sjm.apfv(md, t, g) with t = 37.
import("stdfaust.lib");
sjm = library("seam.moorer.lib");
process = sjm.apfv(1024, 37, 1/sqrt(2));
