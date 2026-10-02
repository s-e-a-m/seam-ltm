// probes.dsp -- the choir's chain, one point per probe.
import("stdfaust.lib");
// the original's last stage, on a sine at 48 Hz (first partial of channels 0-1)
dcb = os.osc(48) : fi.dcblocker;
dry = os.osc(48);
// a noise band as the voices have it, 48 Hz, Q = 350: impulse response
// (its energy times the variance of no.noise, 1/3, is the band's power)
bandir = fi.svf.bp(48, 350);
// the follower alone: a 48 Hz sine switched off at 1 s, release 1.5 s
gate = ba.time < ma.SR;
foll = os.osc(48) * gate : an.amp_follower(1.5);
// the analysis band alone, same input: how long it rings
ring = os.osc(48) * gate : fi.svf.bp(48, 350);
// centres above the Nyquist frequency, reachable from the sliders (f*k^a)
nyq55 = no.noise : fi.svf.bp(0.55 * ma.SR, 350);
nyq75 = no.noise : fi.svf.bp(0.75 * ma.SR, 350);
