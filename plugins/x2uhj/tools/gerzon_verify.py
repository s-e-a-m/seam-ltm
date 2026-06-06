"""Inverse verification of Gerzon's UHJ C-format coefficients.

We feed a planewave at azimuth az into the published UHJ matrix, then read
the localization vectors r_V and r_E under two listening models:
  SURROUND      — the two channels feed a symmetric loudspeaker decode;
  SUPER_STEREO  — L/R feed two frontal loudspeakers at ±30°.
Small angular error across azimuth shows the published coefficients realize
good localization, which is the inverse-verification claim.
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SURROUND = "surround"
SUPER_STEREO = "super_stereo"

# Horizontal B-format (FuMa) of a planewave at azimuth az.
def bformat(az):
    return np.array([1.0 / np.sqrt(2.0), np.cos(az), np.sin(az)])  # W, X, Y

# UHJ C-format Σ/Δ with the j term applied as a real quadrature on a planewave
# (a planewave is analytic, so j multiplies by +90°, i.e. a complex unit).
def uhj_lr(az):
    W, X, Y = bformat(az)
    Sigma = 0.9396926 * W + 0.1855740 * X
    Delta = 1j * (-0.3420201 * W + 0.5098604 * X) + 0.6554516 * Y
    L = 0.5 * (Sigma + Delta)
    R = 0.5 * (Sigma - Delta)
    return L, R

def _vectors(gains, dirs):
    g = np.abs(np.asarray(gains))
    u = np.asarray(dirs, dtype=float)
    rv = (g[:, None] * u).sum(0) / max(g.sum(), 1e-12)
    e = g * g
    re = (e[:, None] * u).sum(0) / max(e.sum(), 1e-12)
    return rv, re

def localization_vectors(az, model):
    L, R = uhj_lr(az)
    if model == SUPER_STEREO:
        dirs = [(np.cos(np.radians(30)),  np.sin(np.radians(30))),
                (np.cos(np.radians(-30)), np.sin(np.radians(-30)))]
        return _vectors([L, R], dirs)
    # SURROUND: a minimal symmetric 4-speaker decode of L/R back to the plane.
    speakers = [0, 90, 180, 270]
    dirs = [(np.cos(np.radians(a)), np.sin(np.radians(a))) for a in speakers]
    # Pantophonic re-encode of the two channels (illustrative symmetric decode).
    gains = [ (L + R).real,  (L - R).real, (L + R).real * 0.5, (L - R).real * 0.5 ]
    return _vectors(gains, dirs)

def main():
    az = np.linspace(0, 2 * np.pi, 180, endpoint=False)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    for ax, model in zip(axes, (SURROUND, SUPER_STEREO)):
        err = []
        for a in az:
            rv, _ = localization_vectors(a, model)
            err.append(np.degrees(abs(np.angle(np.exp(1j*(np.arctan2(rv[1], rv[0]) - a))))))
        ax.plot(np.degrees(az), err)
        ax.set_title(f"r_V angular error — {model}")
        ax.set_xlabel("source azimuth (deg)"); ax.set_ylabel("error (deg)")
        ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig("../doc/math/figures/gerzon_localization.png", dpi=120)
    print("wrote ../doc/math/figures/gerzon_localization.png")

if __name__ == "__main__":
    main()
