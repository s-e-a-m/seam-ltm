declare name      "SEAM SDMX";
declare vendor    "SEAM";
declare version   "0.1.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "Sum-and-Difference Matrix — Blumlein MS encoder/decoder";
//
// SDMX — Normalized sum-and-difference matrix
//
// The Blumlein matrix converts between LR and MS representations:
//
//   M = (L + R) / sqrt(2)   — normalized sum (Mid)
//   S = (L - R) / sqrt(2)   — normalized difference (Side)
//
// The matrix is involutory (A = A^-1): applying it twice returns
// the original signal. The same operation encodes LR→MS and
// decodes MS→LR.
//
// The 1/sqrt(2) normalization preserves signal energy:
//   |M|^2 + |S|^2 = |L|^2 + |R|^2
//
// References:
//   - Alan Blumlein, UK patent 394325 (1933)
//   - Michael Gerzon, "Surround sound from 2-channel stereo" (1970)
//
// ─────────────────────────────────────────────────────────────────────
//
import("stdfaust.lib");
//
// Normalized sum of two signals
nsum(x,y) = (x+y)/sqrt(2);
//
// Normalized difference of two signals
ndif(x,y) = (x-y)/sqrt(2);
//
// Sum-and-difference matrix
// 2 inputs (L,R) → 2 outputs (M,S)
// Self-inverse: sdmx(sdmx(L,R)) = (L,R)
sdmx(x,y) = nsum(x,y), ndif(x,y);
//
process = sdmx;
