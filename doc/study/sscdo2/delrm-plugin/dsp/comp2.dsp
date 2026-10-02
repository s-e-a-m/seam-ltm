// The same compressor with other parameters: the library takes them generically.
import("stdfaust.lib");
process = *(4) <: co.compressor_mono(4, -12, 0.005, 0.2),
                  (co.compression_gain_mono(4, -12, 0.005, 0.2) : ba.linear2db);
