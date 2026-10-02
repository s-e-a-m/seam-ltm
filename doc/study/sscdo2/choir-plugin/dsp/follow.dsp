// an.amp_follower(1.5) on a 48 Hz sine switched off at 1 s: the reference
// for Seam::AmpFollower. The input is generated here and in the C++ test by
// the same formula.
import("stdfaust.lib");
process = sin(2*ma.PI*48*ba.time/ma.SR) * (ba.time < ma.SR) : an.amp_follower(1.5);
