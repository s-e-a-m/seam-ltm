declare name      "SEAM BAMODULEX";
declare vendor    "SEAM";
declare version   "0.1.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "AmbiX → Tetrahedral decoder (Gerzon BA-module, AmbiX variant)";
//
// BAMODULEX — AmbiX-domain B-to-A module
//
// Decodes a first-order AmbiX (ACN/SN3D) signal to four loudspeakers
// placed at the vertices of a tetrahedron inscribed in a cube:
//
//   LFU — Left  Front Up
//   RFD — Right Front Down
//   RBU — Right Back  Up
//   LBD — Left  Back  Down
//
// AmbiX channel ordering (ACN): a0 = W, a1 = Y, a2 = Z, a3 = X.
//
// The decoder matrix is orthogonal: each corner sums W and the three
// Cartesian components signed by the corner's position, scaled by 1/2.
//
//   LFU = (a0 + a1 + a2 + a3) / 2
//   RFD = (a0 - a1 - a2 + a3) / 2
//   RBU = (a0 - a1 + a2 - a3) / 2
//   LBD = (a0 + a1 - a2 - a3) / 2
//
// This is the AmbiX-domain analogue of Gerzon's `bamodule` (FuMa).
// Compensation shelving filters (Gerzon 1975) are intentionally
// omitted: the STONE tetrahedral loudspeaker amplifier handles the
// HF/LF correction downstream, so the plugin remains a pure matrix.
//
// References:
//   - Gerzon, "Ambisonics. Part two: Studio techniques" (1975)
//   - Malham, "Space in Music — Music in Space" (1998)
//   - Nachbar et al., "ambiX — A Suggested Ambisonics Format" (2011)
//
// ─────────────────────────────────────────────────────────────────────
//
import("stdfaust.lib");
//
bamodulex(a0,a1,a2,a3) = lfu, rfd, rbu, lbd
with {
    lfu = (a0 + a1 + a2 + a3) / 2;
    rfd = (a0 - a1 - a2 + a3) / 2;
    rbu = (a0 - a1 + a2 - a3) / 2;
    lbd = (a0 + a1 - a2 - a3) / 2;
};
//
process = bamodulex;
