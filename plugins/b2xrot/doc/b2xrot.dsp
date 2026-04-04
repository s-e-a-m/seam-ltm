declare name      "SEAM B2Xrot";
declare vendor    "SEAM";
declare version   "0.1.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "B-format (FuMa) to AmbiX conversion with 3D rotation";
//
// B2Xrot — B-format to AmbiX conversion with 3D rotation
//
// Converts a first-order ambisonic field from B-format (FuMa)
// to AmbiX (ACN/SN3D), then applies a 3D rotation.
//
// The order of operations is: conversion FIRST, rotation AFTER.
// This is essential because rotateYPR assumes ACN order (W,Y,Z,X).
// Applying it to FuMa channels (W,X,Y,Z) would rotate the wrong axes.
//
// B-format (FuMa):  W, X, Y, Z  — with W attenuated by 1/sqrt(2)
// AmbiX (ACN/SN3D): W, Y, Z, X  — with W at unity gain
//
// Conversion (btox):
//   a0 = W * sqrt(2)   (restore full amplitude)
//   a1 = Y             (reorder)
//   a2 = Z             (reorder)
//   a3 = X             (reorder)
//
// References:
//   - Furse-Malham, "A 3-D Sound Primer" (1998)
//   - Nachbar et al., "ambiX — A Suggested Ambisonics Format" (2011)
//   - Gerzon, "Ambisonics. Part two: Studio techniques" (1975)
//
// ─────────────────────────────────────────────────────────────────────
//
import("stdfaust.lib");
//
// ───────────────────── B-format to AmbiX conversion ──────────────────
//
// FuMa (W,X,Y,Z) → ACN/SN3D (W,Y,Z,X) with W rescaling
//
btox(w,x,y,z) = a0, a1, a2, a3
with {
    a0 = w * sqrt(2);  // W: restore unity gain
    a1 = y;            // Y: FuMa → ACN reorder
    a2 = z;            // Z: unchanged
    a3 = x;            // X: FuMa → ACN reorder
};
//
// ─────────────────────── First-order AmbiX rotation ──────────────────
//
// Intrinsic order: Roll → Pitch → Yaw
// Channels in ACN format: a0=W, a1=Y, a2=Z, a3=X
//
rotateYPR(yaw, pitch, roll, a0, a1, a2, a3) =
    a0, a1_ypr, a2_ypr, a3_ypr
with {
    // Roll (X-axis): rotates Y and Z
    a1_r = a1*cos(roll) - a2*sin(roll);
    a2_r = a1*sin(roll) + a2*cos(roll);
    a3_r = a3;
    // Pitch (Y-axis): rotates Z and X
    a1_rp = a1_r;
    a2_rp = a2_r*cos(pitch) + a3_r*sin(pitch);
    a3_rp = -a2_r*sin(pitch) + a3_r*cos(pitch);
    // Yaw (Z-axis): rotates Y and X
    a1_ypr = a1_rp*cos(yaw) - a3_rp*sin(yaw);
    a2_ypr = a2_rp;
    a3_ypr = a1_rp*sin(yaw) + a3_rp*cos(yaw);
};
//
// ─────────────────────── Parameters ───────────────────────────────────
//
yaw   = hslider("[00]YAW Rotation around Z-axis",0,-180,180,0.5) : ma.deg2rad;
pitch = hslider("[01]PITCH Rotation around Y-axis",0,-180,180,0.5) : ma.deg2rad;
roll  = hslider("[02]ROLL Rotation around X-axis",0,-180,180,0.5) : ma.deg2rad;
//
// ─────────────────────── Process ──────────────────────────────────────
//
// 4 B-format inputs (W,X,Y,Z) → AmbiX conversion → rotation →
// 4 AmbiX outputs (W,Y,Z,X)
//
process = btox : rotateYPR(yaw, pitch, roll);
