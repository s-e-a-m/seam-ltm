// The spec: delRM's four channels, one distance (the entry "mt").
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
mt = nentry("mt", 7.291, 0, 30, 0.001);
process = sdt.delrmcomb(mt), (sdt.delrmrm(mt) : sdt.delrmdyn),
          sdt.delrmcomb(mt), (sdt.delrmrm(mt) : sdt.delrmdyn);
