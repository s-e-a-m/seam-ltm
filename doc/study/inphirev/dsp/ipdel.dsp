// The delays of sgs.ipdel over the id grid at the "j" entry: 9 ids x 4 faces x 18 sections,
// output index (id*4 + k)*18 + i. ipphase takes id through ba.take, so id must be
// a compile-time constant: it is the par index, not an entry.
import("stdfaust.lib");
sgs = library("seam.silvi.lib");
j = nentry("j", 10, 0, 14, 1);
process = par(id, 9, par(k, 4, par(i, 18, sgs.ipdel(sgs.ipgrid(id, j), i, k))));
