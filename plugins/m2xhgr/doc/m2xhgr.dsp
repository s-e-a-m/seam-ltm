declare name      "SEAM M2XHGR";
declare vendor    "SEAM";
declare version   "0.2.0";
declare author    "Giuseppe Silvi";
declare license   "GPL-3.0";
declare description "Mono to first-order spatial field via Haar wavelet decomposition";
//
// M2XHGR — Mono to first-order AmbiX via Haar wavelet
//
// Creates a 4-channel spatial representation from a mono signal
// using the Haar wavelet transform as a filter bank.
//
// The Haar wavelet is the simplest wavelet: a 2-point DFT applied
// sample-by-sample as a running filter pair (QMF):
//
//   sum(x) = (x[n] + x[n-1]) / 2   — lowpass  (DC component)
//   dif(x) = (x[n] - x[n-1]) / 2   — highpass (AC component)
//
// Applied recursively, this decomposes the signal into frequency-ordered
// components. The reversed form (dft2r) outputs highpass first, so that
// the recursive QMF bank produces outputs ordered from highpass to
// lowpass — matching the ACN channel convention where negative-m
// harmonics (left side of the pyramid) carry high-frequency content
// and positive-m harmonics (right side) carry low-frequency content.
//
// haarmn(N) generates (N+1)^2 spatial components from a mono input:
//   - channel 0: passthrough (W, omnidirectional)
//   - channels 1 to (N+1)^2-1: Haar components, highpass to lowpass
//
// For N=1 (first order), the 4 outputs are:
//   a0 = passthrough   (W — full spectrum, unfiltered)
//   a1 = dif           (Y — highpass, first-level difference)
//   a2 = dif_of_sum    (Z — bandpass, second-level difference)
//   a3 = sum_of_sum    (X — lowpass, second-level sum)
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
yaw   = hslider("[00]YAW Rotation around Z-axis",0,-180,180,0.5) : ma.deg2rad;
pitch = hslider("[01]PITCH Rotation around Y-axis",0,-180,180,0.5) : ma.deg2rad;
roll  = hslider("[02]ROLL Rotation around X-axis",0,-180,180,0.5) : ma.deg2rad;
//
// ─────────────────────── Process ──────────────────────────────────────
//
// Mono input → Haar decomposition → rotation → 4-channel AmbiX output
//
process = m2xhgr : rotateYPR(yaw, pitch, roll);
