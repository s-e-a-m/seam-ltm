"""Overlay 2023 empirical vs analytic quadrature error (Re^2+Im^2 proxy)."""
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from rbj import cascade_phase

FS = 48000.0
freqs = np.geomspace(20.0, 20000.0, 512)

EMP_HR = [(374.1, 0.1093), (666.8, 0.4210), (17551.0, 0.5750)]
EMP_HI = [(35.61, 0.2571), (3723.0, 0.3464), (6786.0, 0.1200)]

with open("coeffs.json") as fp:
    d = json.load(fp)
ANA_HR = [(s["f"], s["Q"]) for s in d["H_R"]]
ANA_HI = [(s["f"], s["Q"]) for s in d["H_I"]]

def err_deg(hr, hi):
    diff = cascade_phase(hi, FS, freqs) - cascade_phase(hr, FS, freqs)
    return np.degrees(np.abs(diff - (-np.pi/2)))

plt.figure(figsize=(9, 5))
plt.semilogx(freqs, err_deg(EMP_HR, EMP_HI), label="empirical 2023")
plt.semilogx(freqs, err_deg(ANA_HR, ANA_HI), label="analytic (this work)")
plt.axhline(0, color="k", lw=0.5)
plt.xlabel("Hz"); plt.ylabel("|phase diff − 90°|  (deg)")
plt.title("UHJ quadrature error: empirical vs analytic")
plt.legend(); plt.grid(True, which="both", alpha=0.3)
plt.tight_layout()
plt.savefig("../doc/quadrature_validation.png", dpi=120)
print("wrote ../doc/quadrature_validation.png")
