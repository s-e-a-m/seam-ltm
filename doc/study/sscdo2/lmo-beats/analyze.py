#!/usr/bin/env python3
"""analyze.py -- LMO with two beating oscillators: self-test, measurements, renders.

usage (from this folder, with the seam-ltm venv):
    ../../../../.venv/bin/python analyze.py selftest|measure|render|all

Every number comes from an executed binary built by ./build.sh.
"""
import math
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "bin")
WORK = os.path.join(HERE, "build", "work")
RENDERS = os.path.join(HERE, "renders")
RESULTS_MD = os.path.join(HERE, "results.md")
os.makedirs(WORK, exist_ok=True)

SR = 48000
F = 97.44                 # slider 96 x 1.015: cue 1
DELTAS = [0, 3, 7, 10, 20, 40]
PRE = 1.0                 # pre-roll, discarded: the filters ring for ~0.3 s
MEAS_S = 60.0             # measurement length
COMMANDS = []


def run(probe, seconds, **kw):
    out = os.path.join(WORK, f"out_{os.getpid()}.f64")
    n = int(seconds * SR)
    args = [os.path.join(BIN, probe), str(SR), str(n), f"out={out}", f"pre={PRE}", f"f={F}"]
    args += [f"{k}={v}" for k, v in kw.items()]
    COMMANDS.append(" ".join(os.path.relpath(a, HERE) if a.startswith(HERE) else a for a in args))
    subprocess.run(args, check=True, capture_output=True)
    y = np.fromfile(out, dtype=np.float64)
    os.remove(out)
    if len(y) != n:
        raise RuntimeError(f"{probe}: expected {n} samples, got {len(y)}")
    return y


def rms_db(y):
    return 20 * math.log10(math.sqrt(float(np.mean(y ** 2))))


def corr(a, b):
    return float(np.corrcoef(a, b)[0, 1])


def env_psd(y):
    """Welch PSD of the Hilbert envelope (mean removed), 0.5 Hz resolution."""
    e = np.abs(signal.hilbert(y))
    e = e - e.mean()
    fr, p = signal.welch(e, fs=SR, nperseg=2 * SR, noverlap=SR)
    return fr, p


def peak_near(fr, p, target, lo=1.0, hi=80.0):
    """Frequency of the envelope-spectrum maximum in [lo, hi] Hz."""
    m = (fr >= lo) & (fr <= hi)
    return float(fr[m][np.argmax(p[m])])


def excess_centroid(fr, p, p0, lo=3.0, hi=80.0):
    """Centroid of the envelope spectrum's excess over the d = 0 baseline.
    The beat of two noise bands is a broad, noisy bump at d (about twice the
    band's width) riding on each band's own fluctuation, which is stronger near
    0 Hz: neither the absolute maximum nor the maximum of a ratio finds it
    reliably (the first stays near 4 Hz, the second leans to the bump's upper
    side where the baseline falls fastest), so its centre of mass is taken."""
    m = (fr >= lo) & (fr <= hi)
    x = np.clip(p[m] - p0[m], 0, None)
    return float(np.sum(fr[m] * x) / np.sum(x)) if np.sum(x) > 0 else float("nan")


def band_db(fr, p, fc, half=1.0):
    m = (fr >= fc - half) & (fr <= fc + half)
    return 10 * math.log10(float(np.mean(p[m])))


# ---- self-test --------------------------------------------------------------------
def selftest():
    ok = True

    def expect(name, errs, should_fail):
        nonlocal ok
        good = bool(errs) == should_fail
        ok &= good
        print(f"  [{'ok' if good else 'WRONG'}] {name}: {'FAIL' if errs else 'pass'}"
              f"{' (expected FAIL)' if should_fail else ''} {'; '.join(errs)}")

    s = 20.0
    one = rms_db(run("one_ch", s, ch=0))

    def check_level(y):
        e = rms_db(y) - one
        return [f"level {e:+.2f} dB re one oscillator, expected 0 +- 0.5"] if abs(e) > 0.5 else []

    expect("lmo2 at d=0 has the level of one oscillator", check_level(run("lmo2_ch", s, ch=0, d=0)), False)
    expect("MUTATION two calls at d=0", check_level(run("twocall_ch", s, ch=0, d=0)), True)

    def check_same(a, b):
        m = float(np.max(np.abs(a - b)))
        return [f"max |lib - lmo2| = {m:.2e}, expected 0"] if m != 0 else []

    y2 = run("lmo2_ch", s, ch=1, d=20)
    expect("sdt.lmo equals the prototype lmo2", check_same(run("lib_ch", s, ch=1, d=20), y2), False)
    expect("MUTATION sdt.lmo at d=21 against lmo2 at d=20", check_same(run("lib_ch", s, ch=1, d=21), y2), True)

    a0, b0 = run("osc_ch", s, ch=0, d=0), run("osc_ch", s, ch=4, d=0)

    def check_indep(a, b):
        r = corr(a, b)
        return [f"r(A0,B0) = {r:+.3f}, expected |r| < 0.05"] if abs(r) >= 0.05 else []

    expect("A0 and B0 independent at d=0", check_indep(a0, b0), False)
    expect("MUTATION A0 against itself", check_indep(a0, a0), True)

    fr, p = env_psd(run("lmo2_ch", MEAS_S, ch=0, d=20))
    _, p0 = env_psd(run("lmo2_ch", MEAS_S, ch=0, d=0))

    def check_peak(expected):
        pk = excess_centroid(fr, p, p0)
        return [f"envelope peak {pk:.1f} Hz, expected {expected} +- 2"] if not abs(pk - expected) <= 2 else []

    expect("beat bump at d=20 (re the d=0 baseline)", check_peak(20), False)
    expect("MUTATION envelope peak read against d=30", check_peak(30), True)
    print("SELFTEST", "OK" if ok else "FAILED")
    return ok


# ---- measurements -----------------------------------------------------------------
def measure():
    rows = []
    fr0, p0 = None, None
    ys = {}
    for d in DELTAS:
        y = run("lmo2_ch", MEAS_S, ch=0, d=d)
        ys[d] = y
        fr, p = env_psd(y)
        if d == 0:
            fr0, p0 = fr, p
        rows.append((d, fr, p))
    # intrinsic envelope bandwidth at d=0: where the envelope PSD falls 10 dB below its low-frequency level
    ref = band_db(fr0, p0, 1.0, half=0.5)
    m = fr0 > 0.5
    below = fr0[m][10 * np.log10(p0[m]) < ref - 10]
    env_bw10 = float(below[0]) if len(below) else float("nan")

    L = []
    A = L.append
    A("# LMO with two beating oscillators — results")
    A("")
    A("Generated by `analyze.py measure`; overwritten on every run.")
    A(f"Sample rate {SR} Hz, double precision, f = {F} Hz (cue 1), channel 0, {MEAS_S:.0f} s after a {PRE:.0f} s pre-roll.")
    A("Envelope: |Hilbert| of the output, mean removed; its spectrum by Welch, 2 s segments (0.5 Hz resolution).")
    A("")
    A("## Envelope spectrum")
    A("")
    A("A band of noise fluctuates by itself: its envelope spectrum at d = 0 is the baseline.")
    A(f"At d = 0 it falls 10 dB below its level at 1 Hz by {env_bw10:.1f} Hz: fluctuations faster than that are the beating's alone.")
    A("")
    A("| d Hz | RMS dBFS | envelope maximum Hz (3–80 Hz) | centroid of the excess over d = 0, Hz | envelope level at d, re d = 0 at the same frequency, dB | r(ch0, ch1) |")
    A("|---|---|---|---|---|---|")
    for d, fr, p in rows:
        c1 = run("lmo2_ch", 20.0, ch=1, d=d)
        r01 = corr(ys[d][: len(c1)], c1)
        if d == 0:
            A(f"| 0 | {rms_db(ys[d]):.2f} | {peak_near(fr, p, 0, lo=3.0):.1f} | — | — | {r01:+.3f} |")
        else:
            ex = band_db(fr, p, d) - band_db(fr0, p0, d)
            A(f"| {d} | {rms_db(ys[d]):.2f} | {peak_near(fr, p, d, lo=3.0):.1f} | {excess_centroid(fr, p, p0):.1f} | {ex:+.1f} | {r01:+.3f} |")
    A("")
    A("Envelope spectra, dB re the d = 0 spectrum at 1 Hz, at selected frequencies:")
    A("")
    fs_show = [1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 50]
    A("| d Hz | " + " | ".join(f"{x} Hz" for x in fs_show) + " |")
    A("|---|" + "---|" * len(fs_show))
    for d, fr, p in rows:
        A(f"| {d} | " + " | ".join(f"{band_db(fr, p, x, half=0.5) - ref:+.1f}" for x in fs_show) + " |")
    A("")
    A("## Commands run")
    A("")
    A("```")
    seen = set()
    for c in COMMANDS:
        k = c.split()[0]
        if k not in seen:
            seen.add(k)
            A(c)
    A("```")
    A("")
    NOTES = "<!-- hand-written notes below this line are preserved by analyze.py -->"
    old = open(RESULTS_MD).read() if os.path.exists(RESULTS_MD) else ""
    notes = old.split(NOTES, 1)[1] if NOTES in old else "\n"
    with open(RESULTS_MD, "w") as fh:
        fh.write("\n".join(L) + "\n" + NOTES + notes)
    print("\n".join(L))


# ---- renders ----------------------------------------------------------------------
TARGET = -20.0
FADE = 0.05


def fade(y):
    n = int(FADE * SR)
    w = 0.5 - 0.5 * np.cos(np.pi * np.arange(n) / n)
    y = y.copy()
    y[:n] *= w[:, None] if y.ndim == 2 else w
    y[-n:] *= (w[::-1])[:, None] if y.ndim == 2 else w[::-1]
    return y


def write(name, y, log):
    y = fade(y)
    g = 10 ** (TARGET / 20) / math.sqrt(float(np.mean(y ** 2)))
    y = y * g
    pk = float(np.max(np.abs(y)))
    if pk >= 1.0:
        raise RuntimeError(f"{name} would clip")
    sf.write(os.path.join(RENDERS, name), y, SR, subtype="PCM_24")
    log.append(f"| {name} | {y.shape[1] if y.ndim == 2 else 1} | {y.shape[0] / SR:.0f} | {20 * math.log10(g):+.2f} | {20 * math.log10(pk):.2f} |")


def render():
    log = []
    write("one_osc_mono.wav", run("one_ch", 20.0, ch=0), log)
    for d in DELTAS:
        write(f"d{d:02d}_mono.wav", run("lmo2_ch", 20.0, ch=0, d=d), log)
        write(f"d{d:02d}_4ch.wav", np.stack([run("lmo2_ch", 10.0, ch=c, d=d) for c in range(4)], axis=1), log)
    write("sweep_d00-40_mono.wav", run("lmo2_ch", 60.0, ch=0, mode=1, d1=40, dur=60), log)
    with open(os.path.join(RENDERS, "render-log.md"), "w") as fh:
        fh.write(f"All renders normalised to {TARGET:.0f} dBFS RMS (all channels together), 50 ms fades, 48 kHz, 24-bit.\n\n"
                 "| file | channels | seconds | gain dB | peak dBFS |\n|---|---|---|---|---|\n" + "\n".join(log) + "\n")
    print("\n".join(log))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    if cmd in ("selftest", "all") and not selftest():
        sys.exit(1)
    if cmd in ("measure", "all"):
        measure()
    if cmd in ("render", "all"):
        render()
