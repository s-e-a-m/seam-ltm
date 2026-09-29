// probe.dsp -- delRM block 5: the fi.dcblocker at the end of each delRM channel, and
// the second one the original's process puts after the master volume (block 6).
//
//   comb      sdt.delrmcomb(mt), channels 1 and 3
//   combhp    comb : fi.dcblocker : fi.dcblocker, the two in series, as in the performance
//   trip      sdt.delrmrm(mt) : sdt.delrmdyn, channels 2 and 4 with the leaky integrator
//   triphp    trip : fi.dcblocker : fi.dcblocker
//   triporig  channels 2 and 4 with the original's unbounded fi.integrator, up to the
//             compressor: what the original's DC blockers received
//   dry       the input
//   dcb       fi.dcblocker alone: zero(1) : pole(0.995), a pole fixed in samples
//   dcbat     fi.dcblockerat(fb), fb = 96000/pi * 0.005/1.995 = 76.59 Hz: the same pole at
//             96 kHz, normalised to unity gain at Nyquist (dcb = dcbat / 0.99749...)
//   dcbatx    fi.dcblockerat(76): a mutation that the check must tell apart from dcbat
import("seam.lib");

mt = hslider("mt", 7.291, 0, 30, 0.001);

dcb2 = fi.dcblocker : fi.dcblocker;
fb96 = 96000/ma.PI * 0.005/1.995;

comb   = sdt.delrmcomb(mt);
combhp = comb : dcb2;
trip     = sdt.delrmrm(mt) : sdt.delrmdyn;
triphp   = trip : dcb2;
triporig = _ <: de.delay(1 << 15, sma.imt2npsamp(mt)), _, fi.integrator : *, _ : *
       : *(10) : co.compressor_mono(11, -24, 0.03, 0.04);
dry    = _;
dcb    = fi.dcblocker;
dcbat  = fi.dcblockerat(fb96);
dcbatx = fi.dcblockerat(76);

process = trip;
