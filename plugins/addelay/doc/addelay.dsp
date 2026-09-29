import("seam.lib");
// Distance -> prime integer-sample delay (de.delay fed by sma.imt2npsamp, as in
// ddelay; c = 331.4 m/s; sff.np is a foreign function, C/C++ only) + air-absorption
// filter (sfi air functions). Four channels share one distance/filter.
distance    = hslider("Distance [unit:m]", 0, 0, 30, 0.01);
temperature = hslider("Temperature [unit:degC]", 20, -20, 50, 0.1);
humidity    = hslider("Humidity [unit:pct]", 50, 0, 100, 0.1);
process = par(i, 4, de.delay(1 << 15, sma.imt2npsamp(distance)) : sfi.airCascade);
