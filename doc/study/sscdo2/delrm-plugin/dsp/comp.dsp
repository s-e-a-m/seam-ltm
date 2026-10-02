// co.compressor_mono with delRM's parameters, and its gain in dB. The input
// is scaled by 4 so that the signal's steps cross the threshold.
import("stdfaust.lib");
process = *(4) <: co.compressor_mono(11, -24, 0.03, 0.04),
                  (co.compression_gain_mono(11, -24, 0.03, 0.04) : ba.linear2db);
