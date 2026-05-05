declare name      "SEAM DDELAY";
declare vendor    "SEAM";
declare version   "0.1.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "Quad speaker-alignment delay (interior c, nextprime quantized)";
//
// DDELAY — Quad Speaker-Alignment Delay
//
// Pure integer-sample delay for time-aligning loudspeakers in a
// sound-reinforcement chain. The user enters the speaker distance
// in metres; the plugin converts to samples using the interior
// speed of sound (c = 331.4 m/s) and quantizes to the next prime.
//
// Across multiple plugin instances driving different speakers, the
// prime quantization makes the delay lengths mutually incommensurate
// — the same principle that staggers Schroeder allpass lengths in
// reverbs, here applied to acoustic loudspeaker pathing.
//
// Within one instance the four channels share the same delay
// (synchronous). Per-speaker incommensurability comes from running
// independent instances on different bus paths.
//
// FAUST PROTOTYPE (this file, illustrative — the C++ plugin
// performs the same computation plus the prime quantization):
//
import("stdfaust.lib");
//
// Interior speed of sound (matches seam.math.lib::isos)
isos = 331.4;
//
// Distance → samples (rounded down)
imt2samp(mt) = int(mt * ma.SR / isos);
//
// User control: distance in metres
distance = hslider("Distance [unit:m]", 0, 0, 30, 0.01);
//
// Quad delay, all channels synchronous, no interpolation
process = par(i, 4, de.delay (1 << 15, imt2samp(distance)));
