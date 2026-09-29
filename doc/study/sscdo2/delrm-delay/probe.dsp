import("seam.lib");
off = hslider("off", 0, -1, 1, 0.001);   // mutation hook: a whole-millimetre offset
process = sma.imt2npsamp(ba.time * 0.0001 + off);
