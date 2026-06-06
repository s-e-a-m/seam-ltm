// SEAM-LTM · ABMODULEX — Faust spec / block-diagram source.
//
// Canonical DSP: sam.abmodulex in seam.ambisonics.lib
//   A-format tetrahedral mic (LFU,RFD,RBU,LBD) -> First-order AmbiX (ACN).
//   Inverse of bamodulex; the matrix is involutory. The C++ plugin
//   re-implements this by hand (see plugins/abmodulex/source).
//
// Regenerate diagrams:
//   ../../tools/gen-faust-doc.sh abmodulex
//
import("seam.lib");
process = sam.abmodulex;
