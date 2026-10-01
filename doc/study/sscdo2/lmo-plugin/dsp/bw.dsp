// Order-24 and order-2 Butterworth on the current SVF faustlibraries:
// the reference for Seam::ButterworthSVF. Driven by an impulse.
import("stdfaust.lib");
process = _ <: fi.highpass(24, 97.44), fi.lowpass(24, 97.44),
               fi.highpass(2, 1000), fi.lowpass(2, 1000);
