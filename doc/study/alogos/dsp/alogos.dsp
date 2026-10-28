// The spec: sgs.alogos(r, D, g, N, id, j). gen-ref.sh substitutes the lines
// below (scale, density, gain, sections per face, STONE id, grid point).
import("stdfaust.lib");
sma = library("seam.math.lib");
sgs = library("seam.silvi.lib");
r = sma.phi(1);
D = 1;
g = sma.rphi(1);
N = 4;
id = 1;
j = 10;
process = sgs.alogos(r, D, g, N, id, j);
