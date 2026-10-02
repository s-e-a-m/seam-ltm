// sfi.leakyint(1): the integral in seconds, forgetting below 1 Hz.
import("stdfaust.lib");
sfi = library("seam.filters.lib");
process = sfi.leakyint(1);
