// The spec with the compressor's sliders away from the score, a different set
// on channels 2 and 4, so that a set reaching the wrong channel shows. The
// threshold of channel 2 is the entry "t2", moved during the run by gen-ref.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
mt = 7.291;
t2 = nentry("t2", -12, -60, 0, 0.001);
process = sdt.delrmcomb(mt),
          (sdt.delrmrm(mt) : sdt.delrmdynw(ba.db2linear(30), 4, t2, 0.005, 0.2)),
          sdt.delrmcomb(mt),
          (sdt.delrmrm(mt) : sdt.delrmdynw(ba.db2linear(6), 20, -36, 0.1, 1.0));
