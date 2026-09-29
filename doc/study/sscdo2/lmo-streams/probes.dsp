import("seam.lib");
ch = hslider("ch", 0, 0, 7, 1);
// Giuseppe's test, verbatim
lmoGS(N,M,f) = no.multinoise(N) <: par(i,N, par(i, M, sdt.lmoband(f + i))) :> si.bus(M) : par(i,4,/(N));
gs1 = lmoGS(1,4,1000) : ba.selectn(4, ch);
gs2 = lmoGS(2,4,1000) : ba.selectn(4, ch);
// the 8 signals before the merge, N=2: outer block 0 = 0..3, outer block 1 = 4..7
pre2 = no.multinoise(2) <: par(i,2, par(i,4, sdt.lmoband(1000 + i))) : ba.selectn(8, ch);
// Davide's structure: one multinoise(4), one stream per channel
dav = sdt.lmoosc(4, 1000) : ba.selectn(4, ch);
process = gs1;
