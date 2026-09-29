// probe.dsp -- delRM block 3: the triple product x[n-D] * x * integral(x), with
// three integrators, and the original's stage after it (x10, compressor, DC blocker).
//
//   I0   fi.integrator (+ ~ _), as in the original: a pole at z = 1, unbounded state,
//        gain SR/(2 pi f) on a sinusoid, so its level doubles with the sample rate.
//   I1   leaky integrator, normalised to the original's level at 48 kHz:
//        y = (48000/SR) * x + a * y[n-1], a = exp(-2 pi fc / SR). Above fc it is
//        the original at 48 kHz at every rate; below fc it forgets, so its state
//        stays bounded. 48 kHz is a provisional anchor: the performance rate is
//        still to be confirmed by Davide.
//   I2   fi.dcblockerat(5) before fi.integrator: the remedy that removes the DC
//        of the input but keeps the pole at z = 1.
//
// For each Ik:
//   intk   the integrator alone (its state)
//   prek   10 * x[n-D] * x * Ik(x), before the compressor
//   postk  prek : co.compressor_mono(11, -24, 0.03, 0.04) : fi.dcblocker (the original's stage)
//   selfrm 10 * x[n-D] * x, the product without the integral (what I0 degenerates into
//          when a large drift turns the integral into a constant)
//
// Controls: mt (DDELAY distance, m), fc (I1's corner, Hz).
import("seam.lib");

mt = hslider("mt", 7.291, 0, 30, 0.001);
fc = hslider("fc", 1, 0.01, 20, 0.01);

xD = sma.imnpdelay(1 << 15, mt);

I0 = fi.integrator;
I1 = *(48000 / ma.SR) : + ~ *(exp(-2 * ma.PI * fc / ma.SR));
I2 = fi.dcblockerat(5) : fi.integrator;

triple(I) = _ <: xD, _, I : * , _ : * : *(10);
stage   = co.compressor_mono(11, -24, 0.03, 0.04) : fi.dcblocker;

int0 = I0;  pre0 = triple(I0);  post0 = pre0 : stage;
int1 = I1;  pre1 = triple(I1);  post1 = pre1 : stage;
int2 = I2;  pre2 = triple(I2);  post2 = pre2 : stage;
selfrm = _ <: xD, _ : * : *(10);

process = pre0;
