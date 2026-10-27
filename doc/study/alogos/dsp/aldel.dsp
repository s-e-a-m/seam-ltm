// The spec's delay, sgs.aldel(r, D, d, i, k), with every argument an entry, and
// the spec's grid, sgs.algrid(r, D, id, j), for the ids 0..4 at the "j" entry.
// Outputs: 0..4 = algrid(r, D, id, j) for id 0..4; 5 = aldel(r, D, d, i, k).
// alphase takes id through ba.take, so id must be a compile-time constant: it
// is the par index, not an entry. The step of every entry is 0: refdump writes
// the zones directly and the DSP reads them unquantised (see gen-ref.sh).
import("stdfaust.lib");
sgs = library("seam.silvi.lib");
r = nentry("r", 1.618, 1, 4, 0);
D = nentry("D", 1, 1, 4, 0);
j = nentry("j", 10, 0, 81, 0);
d = nentry("d", 3.33, 0, 8, 0);
i = nentry("i", 0, 0, 127, 0);
k = nentry("k", 0, 0, 3, 0);
process = par(id, 5, sgs.algrid(r, D, id, j)), sgs.aldel(r, D, d, i, k);
