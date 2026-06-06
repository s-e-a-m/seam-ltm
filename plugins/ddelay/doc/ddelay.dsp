import("seam.lib");
// Canonical DSP: sma.imdelay in seam.math.lib (interior distance delay).
// The plugin runs four parallel channels (quad speaker alignment).
distance = hslider("Distance [unit:m]", 0, 0, 30, 0.01);
process = par(i, 4, sma.imdelay(distance));
