// SEAM-LTM · MULTIPINK — Faust spec / block-diagram source.
//
// Canonical DSP: sno.multipink in seam.noises.lib
//   multipink(N,g) = no.multinoise(N) : par(i,N, no.pink_filter : *(g));
// N independent pink-noise streams at gain g. The C++ plugin re-implements
// this by hand (see plugins/multipink/source). This file exists to generate
// the block diagram and to document the canonical Faust reference.
//
// Regenerate diagrams:
//   faust -I ../../../../faust-libraries/src -svg multipink.dsp
//
import("seam.lib");
process = sno.multipink(4, 0.5);
