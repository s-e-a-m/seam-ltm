// SEAM-LTM · ABMODULEX — Faust spec / block-diagram source.
//
// Canonical DSP: sam.abmodulexw(dual) in seam.ambisonics.lib
//   A-format tetrahedral mic -> First-order AmbiX (ACN), the slots read as
//   LFU,RFD,RBU,LBD (dual = 0, sam.abmodulex, involutory, the inverse of
//   bamodulex) or as the dual tetrahedron LFD,RFU,RBD,LBU (dual = 1: the Z
//   row changes sign). The C++ plugin re-implements this by hand (see
//   plugins/abmodulex/source) and crossfades Z over 25 ms.
//
// Regenerate diagrams:
//   ../../tools/gen-faust-doc.sh abmodulex
//
import("seam.lib");
dual = nentry("dual", 0, 0, 1, 1);
process = sam.abmodulexw(dual);
