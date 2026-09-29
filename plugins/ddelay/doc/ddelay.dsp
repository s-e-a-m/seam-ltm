import("seam.lib");
// Canonical DSP: de.delay fed by sma.imt2npsamp (seam.math.lib): the distance
// rounded to the millimetre, converted at 331.4 m/s, rounded to the nearest
// sample, moved to the next prime. sff.np is a foreign function: compile with
// the C/C++ backends and -I on the seam src directory.
// The plugin runs four parallel channels (quad speaker alignment).
distance = hslider("Distance [unit:m]", 0, 0, 30, 0.01);
process = par(i, 4, de.delay(1 << 15, sma.imt2npsamp(distance)));
