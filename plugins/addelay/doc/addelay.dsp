import("seam.lib");
// Distance -> integer-sample delay (sma.imdelay, c = 331.4 m/s) + air-absorption
// filter (sfi air functions). Four channels share one distance/filter.
distance    = hslider("Distance [unit:m]", 0, 0, 30, 0.01);
temperature = hslider("Temperature [unit:degC]", 20, -20, 50, 0.1);
humidity    = hslider("Humidity [unit:pct]", 50, 0, 100, 0.1);
process = par(i, 4, sma.imdelay(distance) : sfi.airCascade);
