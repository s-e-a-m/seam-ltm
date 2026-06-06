// SEAM-LTM · X2UHJ — Faust spec / block-diagram source.
//
// Canonical DSP: sam.x2uhj in seam.ambisonics.lib
//   AmbiX (ACN/SN3D) -> UHJ C-format L,R,T,Q, via the matched quadrature
//   all-pass pair (sam.hRuhj / sam.hIuhj). Quadrature coefficients were
//   re-derived analytically; the single source of truth is
//   plugins/x2uhj/source/x2uhj_coeffs.h. The C++ plugin re-implements this
//   by hand (see plugins/x2uhj/source). This file generates the block
//   diagram and documents the canonical Faust reference.
//
// Regenerate diagrams:
//   faust -I ../../../../faust-libraries/src -svg x2uhj.dsp
//
import("seam.lib");
process = sam.x2uhj;
