// The same section with another gain: the library takes g as a parameter.
import("stdfaust.lib");
sjm = library("seam.moorer.lib");
process = sjm.apfv(1024, 37, 0.7);
