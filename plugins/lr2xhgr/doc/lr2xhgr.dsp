import("seam.lib");
// Canonical DSP: sam.lr2xhgr (Haar via seam.dwt.lib) in seam.ambisonics.lib
divergence = hslider("[00]DIVERGENCE",90,0,360,0.1) / 2 : ma.deg2rad;
yaw   = hslider("[01]YAW Rotation around Z-axis",0,-180,180,0.5) : ma.deg2rad;
pitch = hslider("[02]PITCH Rotation around Y-axis",0,-180,180,0.5) : ma.deg2rad;
roll  = hslider("[03]ROLL Rotation around X-axis",0,-180,180,0.5) : ma.deg2rad;
process = sam.lr2xhgr(divergence, yaw, pitch, roll);
