// The spec: sdt.stunedrev at the Pd patch's starting times.
import("stdfaust.lib");
sdt = library("seam.tedesco.lib");
t1 = nentry("t1", 83, 1, 100, 1);
t2 = nentry("t2", 47, 1, 100, 1);
t3 = nentry("t3", 7, 1, 100, 1);
t4 = nentry("t4", 71, 1, 100, 1);
process = sdt.stunedrev(t1, t2, t3, t4);
