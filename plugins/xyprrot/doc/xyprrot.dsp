declare name      "SEAM XYPRrot";
declare vendor    "SEAM";
declare version   "0.1.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "First-order AmbiX rotation — Yaw, Pitch, Roll";
//
// XYPRrot — 3D rotation of a first-order AmbiX sound field
//
// Rotates a first-order ambisonic field (4 channels, ACN/SN3D: W, Y, Z, X)
// applying three rotations in intrinsic order: Roll → Pitch → Yaw.
//
//   Roll  (X-axis) — lateral tilt (left-right vertical plane)
//   Pitch (Y-axis) — forward-backward tilt (vertical plane)
//   Yaw   (Z-axis) — horizontal rotation (azimuth)
//
// Channel W (omnidirectional) is unaffected by rotation.
//
// The rotation matrix is the composition R = Rz(yaw) · Ry(pitch) · Rx(roll)
// applied to the three directional channels (Y, Z, X) in ACN order.
//
// References:
//   - Nachbar et al., "ambiX — A Suggested Ambisonics Format" (2011)
//   - Malham, "3-D Sound Spatialization using Ambisonic Techniques" (1999)
//
// ─────────────────────────────────────────────────────────────────────
//
import("stdfaust.lib");
//
// ─────────────────────── First-order AmbiX rotation ───────────────────
//
// Intrinsic order: Roll → Pitch → Yaw
// Inputs and outputs in ACN format: a0=W, a1=Y, a2=Z, a3=X
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
// 4 AmbiX inputs (W,Y,Z,X) → rotation → 4 AmbiX outputs (W,Y,Z,X)
//
process = rotateYPR(yaw, pitch, roll);
