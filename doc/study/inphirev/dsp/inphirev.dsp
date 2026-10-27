// The spec: sgs.inphirev(N, 1, 10). gen-ref.sh substitutes N below (sections per face).
import("stdfaust.lib");
sgs = library("seam.silvi.lib");
N = 4;
process = sgs.inphirev(N, 1, 10);
