// SEAM-LTM · HILBERT — Faust spec / block-diagram source.
//
// Canonical DSP: sfi.hilbertRBJ / sfi.hilbertPoly in seam.filters.lib
//   mono -> (in-phase, quadrature): a wideband 90-degree quadrature pair.
//   Both branches are all-pass filtered; their phase difference holds -90 deg
//   across 20 Hz-20 kHz, so neither branch equals the dry input. The C++ plugin
//   (plugins/hilbert/source) re-implements this by hand and designs the
//   coefficients live per sample rate; this file documents the canonical Faust
//   reference and generates the block diagram for the default (RBJ) topology.
//
// Regenerate diagrams + mathdoc:
//   tools/gen-faust-doc.sh hilbert
//
import("seam.lib");
process = sfi.hilbertRBJ;
