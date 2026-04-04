declare name      "SEAM LR2XHGR";
declare vendor    "SEAM";
declare version   "0.2.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "Stereo to first-order spatial field via Haar wavelet with divergence";
//
// LR2XHGR — Stereo to first-order AmbiX via Haar wavelet with divergence
//
// Extends M2XHGR to stereo: L and R are decomposed independently
// via Haar, positioned in space with opposing rotations (divergence),
// and summed into a single spatial field.
//
// Processing chain:
//   1. Independent Haar decomposition of L and R (haarmn(1))
//   2. Divergent rotation: L rotated by +divergence, R by -divergence
//      (with pitch and roll applied to both)
//   3. Sum the two 4-channel fields into one
//   4. Global rotation (yaw only) to orient the entire field
//
// Divergence controls the angular aperture of the stereo image:
//   0°   → L and R overlapping (mono)
//   90°  → L and R at ±45° (default, natural maximum separation)
//   180° → L and R opposite (maximum divergence)
//
// The DIVERGENCE parameter is halved internally:
// a value of 90° positions L at +45° and R at -45°.
//
// ─────────────────────────────────────────────────────────────────────
//
import("stdfaust.lib");
//
// ───────────────────── 2-point DFT (Haar wavelet) ─────────────────────
//
// Running sum/difference filter pair: the simplest QMF.
// Standard form: lowpass first, highpass second.
//
dft2(x) = s(x), d(x)
with {
    s(x) = (x + x') / 2;   // lowpass  (DC)
    d(x) = (x - x') / 2;   // highpass (AC)
};
//
// Reversed form: highpass first, lowpass second.
// Used in the QMF bank so that outputs are ordered high-to-low,
// matching the ACN channel convention.
//
dft2r(x) = dft2(x) : ro.cross(2);
//
// ───────────────────── Recursive QMF filter bank ──────────────────────
//
// qmf(N) applies dft2r recursively N times, producing N+1 outputs.
// At each level, the highpass component exits and the lowpass
// continues into the next decomposition. The result is a set of
// sub-band signals ordered from highest to lowest frequency.
//
qmf(0) = _;
qmf(1) = dft2r;
qmf(N) = dft2r : _, qmf(N-1);
//
// ───────────────────── Haar spatial decomposition ─────────────────────
//
// haarmn(N): mono input to (N+1)^2 spatial components.
// The passthrough (unfiltered signal) occupies channel 0 (W),
// followed by the QMF sub-bands as directional components.
//
haarmn(0) = _;
haarmn(N) = _ <: _, qmf((N+1)^2-2);
//
// m2xhgr: mono to 4-channel first-order spatial field
//
m2xhgr = haarmn(1);
//
// ─────────────────────── First-order AmbiX rotation ───────────────────
//
// Applies yaw, pitch, roll rotation to a first-order signal.
// Channel 0 (W) passes through unchanged.
//
rotateYPR(yaw, pitch, roll, a0, a1, a2, a3) =
    a0, a1_ypr, a2_ypr, a3_ypr
with {
    a1_r = a1*cos(roll) - a2*sin(roll);
    a2_r = a1*sin(roll) + a2*cos(roll);
    a3_r = a3;
    a1_rp = a1_r;
    a2_rp = a2_r*cos(pitch) + a3_r*sin(pitch);
    a3_rp = -a2_r*sin(pitch) + a3_r*cos(pitch);
    a1_ypr = a1_rp*cos(yaw) - a3_rp*sin(yaw);
    a2_ypr = a2_rp;
    a3_ypr = a1_rp*sin(yaw) + a3_rp*cos(yaw);
};
//
// ─────────────────────── Parameters ───────────────────────────────────
//
divergence = hslider("[00]DIVERGENCE",90,0,180,0.1) / 2 : ma.deg2rad;
yaw   = hslider("[01]YAW Rotation around Z-axis",0,-180,180,0.5) : ma.deg2rad;
pitch = hslider("[02]PITCH Rotation around Y-axis",0,-180,180,0.5) : ma.deg2rad;
roll  = hslider("[03]ROLL Rotation around X-axis",0,-180,180,0.5) : ma.deg2rad;
//
// ─────────────────────── Process ──────────────────────────────────────
//
// Stereo input (L,R) → independent Haar decomposition →
// divergent rotations → sum → global rotation →
// 4-channel AmbiX output
//
lr2xhgr = par(i,2, m2xhgr) :
    rotateYPR(divergence, pitch, roll),
    rotateYPR(0-divergence, pitch, roll)
    :> si.bus(4) :
    rotateYPR(yaw, 0, 0);
//
process = lr2xhgr;
