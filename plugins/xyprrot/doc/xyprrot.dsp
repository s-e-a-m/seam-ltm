import("seam.lib");
// Canonical DSP: sam.rotateYPR in seam.ambisonics.lib
yaw   = hslider("[00]YAW Rotation around Z-axis",0,-180,180,0.5) : ma.deg2rad;
pitch = hslider("[01]PITCH Rotation around Y-axis",0,-180,180,0.5) : ma.deg2rad;
roll  = hslider("[02]ROLL Rotation around X-axis",0,-180,180,0.5) : ma.deg2rad;
process = sam.rotateYPR(yaw, pitch, roll);
