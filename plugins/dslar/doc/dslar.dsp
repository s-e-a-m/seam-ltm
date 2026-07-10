import("seam.lib");
// Canonical DSP: sds.lar in seam.discipio.lib — the feedforward mono LAR
// processor of Di Scipio's LAR.pd (the Larsen loop is acoustic, external).
gate     = checkbox("Power");                             // system on/off, 2000 ms anti-click fade
drive    = hslider("Drive", 1, 1, 4, 0.01);              // audio pre-gain (Pd presets 1/2/4)
target   = hslider("Target", 1, 0, 1, 0.001);            // homeostat reference (Pd: - 1)
steep    = hslider("Steepness", 40, 1, 80, 0.1);         // homeostat exponent (Pd: pow 40)
tsmooth  = hslider("Control smoothing [unit:ms]", 200, 1, 1000, 1);
tab1     = hslider("Loop delay [unit:ms]", 50, 1, 200, 0.1);
tab2     = hslider("Decorrelation [unit:ms]", 20, 1, 200, 0.1);
output   = hslider("Output", 1, 0, 1, 0.001);            // final VCA to host
process  = sds.lar(gate, drive, target, steep, tsmooth, tab1, tab2, output);
