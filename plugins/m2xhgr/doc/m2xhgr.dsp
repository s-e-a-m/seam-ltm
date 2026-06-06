import("seam.lib");
// Canonical DSP: sam.m2xhgr (Haar via seam.dwt.lib) + sam.rotateYPR
yaw   = hslider("[00]YAW Rotation around Z-axis",0,-180,180,0.5) : ma.deg2rad;
pitch = hslider("[01]PITCH Rotation around Y-axis",0,-180,180,0.5) : ma.deg2rad;
roll  = hslider("[02]ROLL Rotation around X-axis",0,-180,180,0.5) : ma.deg2rad;
process = sam.m2xhgr : sam.rotateYPR(yaw, pitch, roll);
