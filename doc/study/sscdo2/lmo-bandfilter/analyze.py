#!/usr/bin/env python3
"""analyze.py -- measurements and listening renders for the LMO band-filter study.

Every number comes from an executed render of a compiled candidate (build.sh).
Usage (from the seam-ltm root venv):
    .venv/bin/python analyze.py selftest   # mutation checks: the checks must catch a mistuned filter
    .venv/bin/python analyze.py measure    # M1, M2, M4 -> build/work/results.json + results.md tables
    .venv/bin/python analyze.py render     # listening renders -> renders/
    .venv/bin/python analyze.py all        # the three above, in order
"""
import json
import math
import os
import random
import subprocess
import sys
import time

import numpy as np
import soundfile as sf
from scipy import signal

# ---- paths -------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "bin")
WORK = os.path.join(HERE, "build", "work")
RENDERS = os.path.join(HERE, "renders")
RESULTS_MD = os.path.join(HERE, "results.md")
os.makedirs(WORK, exist_ok=True)

# ---- the candidates -----------------------------------------------------------
# key: (process name in dsp/cands.dsp, libset, description)
CANDS = {
    "A": ("hplp", "old", "OLD libs: fi.highpass(24,F) : fi.lowpass(24,F-0.0001), direct-form tf2s"),
    "B": ("hplp", "new", "NEW libs: same expression, TPT SVF sections (0965ea2)"),
    "C": ("bp24", "new", "NEW libs: fi.bandpass(24, fl, fu) x0.5, 24 SVF sections, fl/fu = A's -3 dB edges"),
}
ORDER = ["A", "B", "C"]

K1015 = 1.015                       # F = 1.015 * slider (Davide)
RE = (3 + 2 * math.sqrt(2)) ** (1 / 48)   # A's -3 dB edge ratio (warped domain)
SLIDER_96, SLIDER_111 = 96.0, 111.0

COMMANDS = []                        # every harness command actually run, for results.md


def binpath(proc, lib, prec="double", opt="O2"):
    p = os.path.join(BIN, f"{proc}_{lib}_{prec}_{opt}")
    if not os.path.exists(p):
        sys.exit(f"missing binary {p}: run ./build.sh all")
    return p


def run(proc, lib, sr, n, prec="double", opt="O2", log=True, **kw):
    """Run a candidate offline; return its output as float64 (or the bench line)."""
    out = os.path.join(WORK, f"out_{os.getpid()}.f64")
    args = [binpath(proc, lib, prec, opt), str(int(sr)), str(int(n))]
    bench = "bench" in kw
    if not bench:
        args.append(f"out={out}")
    for k, v in kw.items():
        args.append(f"{k}={v}")
    if log:
        COMMANDS.append(" ".join(os.path.relpath(a, HERE) if a.startswith(HERE) else a for a in args))
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"{' '.join(args)}\n{r.stderr}")
    if bench:
        return r.stdout
    y = np.fromfile(out, dtype=np.float64)
    os.remove(out)
    if len(y) != n:
        raise RuntimeError(f"expected {n} samples, got {len(y)}")
    return y


# ---- input: stream 0 of no.multinoise(8), stored as float32 ---------------------
def noise_file(sr, seconds):
    """Stream 0 of no.multinoise(8). multinoise is sample-indexed, so the sequence is the
    same at every SR; the file is float32 so -single and -double see identical input."""
    n = int(math.ceil(seconds * sr))
    path = os.path.join(WORK, f"noise0_{n}.f32")
    if not os.path.exists(path):
        y = run("noise0", "new", sr, n, log=False)
        y.astype(np.float32).tofile(path)
    return path


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-300))


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


# ---- A's -3 dB edges and the parameters that give C/D the same band ----------
EDGES = {}   # (sr, F) -> (fl, fu, "measured"|"analytic")


def analytic_edges(sr, F):
    t = math.tan(math.pi * F / sr)
    return sr / math.pi * math.atan(t / RE), sr / math.pi * math.atan(t * RE)


def edges(sr, F):
    if (sr, F) in EDGES:
        return EDGES[(sr, F)]
    fl, fu = analytic_edges(sr, F)
    return fl, fu, "analytic"


def cparams(key, sr, F):
    fl, fu, _ = edges(sr, F)
    if key == "C":
        return {"rl": f"{F / fl:.9f}", "ru": f"{fu / F:.9f}"}
    return {}


def steady(F):
    return {"mode": 0, "smoo": 0, "f0": f"{F / K1015:.9f}"}


# ---- M1/M2: impulse response -----------------------------------------------------
def impulse_response(key, sr, F, prec="double", extra=None, fparams=None):
    proc, lib, _ = CANDS[key]
    kw = dict(steady(F), **(fparams if fparams is not None else cparams(key, sr, F)), **(extra or {}))
    T = max(4.0, 400.0 / F)
    while True:
        h = run(proc, lib, sr, int(T * sr), prec=prec, **{"in": "impulse"}, **kw)
        e = np.cumsum(h[::-1] ** 2)[::-1]
        tail = e[int(0.9 * len(h))] / e[0]
        if tail < 1e-25 or T > 120:
            return h, 10 * math.log10(max(tail, 1e-300))
        T *= 2


def dtft(h, sr, f):
    n = np.arange(len(h))
    return np.array([np.dot(h, np.exp(-2j * np.pi * fi * n / sr)) for fi in np.atleast_1d(f)])


def response_metrics(h, sr, F):
    lo, hi = F / 1.25, F * 1.25
    m = 40001
    fz = np.linspace(lo, hi, m)
    Hz = signal.zoom_fft(h, [lo, hi], m=m, fs=sr, endpoint=True)
    mag = db(Hz)
    ip = int(np.argmax(mag))
    # parabolic refinement of the peak on the dB grid
    if 0 < ip < m - 1:
        a, b, c = mag[ip - 1], mag[ip], mag[ip + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c)
        fpk, pk = fz[ip] + d * (fz[1] - fz[0]), b - 0.25 * (a - c) * d
    else:
        fpk, pk = fz[ip], mag[ip]
    lvl = pk - 10 * math.log10(2)   # half power: the -3 dB of the analytic response and of fi.bandpass
    il = ip
    while il > 0 and mag[il] > lvl:
        il -= 1
    iu = ip
    while iu < m - 1 and mag[iu] > lvl:
        iu += 1
    fl = np.interp(lvl, [mag[il], mag[il + 1]], [fz[il], fz[il + 1]])
    fu = np.interp(lvl, [mag[iu], mag[iu - 1]], [fz[iu], fz[iu - 1]])
    oct_pts = {}
    for o in (-2, -1, -0.5, -0.25, 0.25, 0.5, 1, 2):
        f = F * 2 ** o
        if f < sr / 2:
            oct_pts[o] = float(db(dtft(h, sr, f))[0])
    # stopband floor: the largest response beyond +-2 octaves (full-band FFT)
    nfft = 1 << int(math.ceil(math.log2(len(h))))
    Hf = db(np.fft.rfft(h, nfft))
    ff = np.fft.rfftfreq(nfft, 1 / sr)
    stop = (ff <= F / 4) | (ff >= 4 * F)
    floor_db = float(Hf[stop].max())
    at_F = dtft(h, sr, F)[0]
    # group delay at F
    n = np.arange(len(h))
    gd = float(np.real(np.dot(n * h, np.exp(-2j * np.pi * F * n / sr)) / at_F)) / sr
    # envelope and energy decay
    env = np.abs(signal.hilbert(h))
    envdb = db(env / env.max())
    ipk = int(np.argmax(env))
    above = np.nonzero(envdb > -60)[0]
    t_env60 = above[-1] / sr
    edc = np.cumsum(h[::-1] ** 2)[::-1]
    edcdb = 10 * np.log10(np.maximum(edc / edc[0], 1e-300))
    t_edc20 = np.nonzero(edcdb > -20)[0][-1] / sr
    t_edc60 = np.nonzero(edcdb > -60)[0][-1] / sr
    # analytic check (A, B): |H|^2 = 1/(2 + r^48 + r^-48), r warped
    r = np.tan(np.pi * fz / sr) / np.tan(np.pi * F / sr)
    theo = -10 * np.log10(2 + r ** 48 + r ** -48)
    sel = theo > -150
    dev = float(np.max(np.abs(mag[sel] - theo[sel])))
    return {
        "peak_db": float(pk), "f_peak": float(fpk), "fl": float(fl), "fu": float(fu),
        "bw": float(fu - fl), "f_centre": float(math.sqrt(fl * fu)), "bw_pct": float(100 * (fu - fl) / F), "q_eq": float(F / (fu - fl)),
        "oct": oct_pts, "gd_ms": 1000 * gd, "t_envpeak_ms": 1000 * ipk / sr,
        "t_env60_ms": 1000 * t_env60, "t_edc20_ms": 1000 * t_edc20, "t_edc60_ms": 1000 * t_edc60,
        "dev_hplp_theory_db": dev, "floor_db": floor_db, "gain_sumsq_db": float(10 * np.log10(np.sum(h ** 2))),
    }


# ---- checks (the self-test breaks them on purpose) ------------------------------
def check_hplp(m, F, sr):
    fl, fu = analytic_edges(sr, F)
    errs = []
    if abs(m["peak_db"] + 6.0206) > 0.05:
        errs.append(f"peak {m['peak_db']:.3f} dB, expected -6.021")
    if abs(m["f_peak"] - F) > 0.05:
        errs.append(f"peak at {m['f_peak']:.3f} Hz, expected {F:.3f}")
    if abs(m["bw"] / (fu - fl) - 1) > 0.005:
        errs.append(f"BW {m['bw']:.4f} Hz, expected {fu - fl:.4f}")
    for o, th in ((0.5, -72.25), (-0.5, -72.25)):
        # theory at +-1/2 octave in the warped domain
        r = math.tan(math.pi * F * 2 ** o / sr) / math.tan(math.pi * F / sr)
        th = -10 * math.log10(2 + r ** 48 + r ** -48)
        if abs(m["oct"][o] - th) > 0.5:
            errs.append(f"{o:+} oct {m['oct'][o]:.2f} dB, expected {th:.2f}")
    return errs


def check_matched(m, ref):
    """C against A: same peak level and the same half-power edges. C's top is maximally
    flat, so its argmax frequency is not a property and is not checked."""
    errs = []
    if abs(m["peak_db"] + 6.0206) > 0.05:
        errs.append(f"peak {m['peak_db']:.3f} dB, expected -6.021")
    tol = 0.005 * ref["bw"]
    for e in ("fl", "fu"):
        if abs(m[e] - ref[e]) > tol:
            errs.append(f"{e} {m[e]:.4f} Hz, A {ref[e]:.4f}")
    return errs


def selftest():
    sr, F = 48000, SLIDER_96 * K1015
    print(f"selftest at SR {sr}, F {F:.3f} Hz")
    ok = True

    def expect(name, errs, should_fail):
        nonlocal ok
        failed = bool(errs)
        good = failed == should_fail
        ok &= good
        print(f"  [{'ok' if good else 'WRONG'}] {name}: {'FAIL' if failed else 'pass'}"
              f"{' (expected FAIL)' if should_fail else ''} {'; '.join(errs)}")

    hA, _ = impulse_response("A", sr, F)
    mA = response_metrics(hA, sr, F)
    expect("A as built", check_hplp(mA, F, sr), False)
    # mutation 1: the whole filter mistuned by +1 Hz (slider moved by 1/1.015)
    h, _ = impulse_response("A", sr, F + 1.0)
    expect("A mistuned +1 Hz", check_hplp(response_metrics(h, sr, F + 1.0), F, sr), True)
    # mutation 2: LP 1 Hz above the HP instead of 0.0001 Hz below
    h, _ = impulse_response("A", sr, F, extra={"lpd": 1.0})
    expect("A with LP at F+1", check_hplp(response_metrics(h, sr, F), F, sr), True)
    hB, _ = impulse_response("B", sr, F)
    expect("B as built", check_hplp(response_metrics(hB, sr, F), F, sr), False)
    h, _ = impulse_response("C", sr, F)
    expect("C matched to A", check_matched(response_metrics(h, sr, F), mA), False)
    # mutation 3: C with the upper edge 1 Hz too high
    p = cparams("C", sr, F)
    h, _ = impulse_response("C", sr, F, fparams={"rl": p["rl"], "ru": float(p["ru"]) + 1.0 / F})
    expect("C with fu +1 Hz", check_matched(response_metrics(h, sr, F), mA), True)
    # the OLD and NEW builds must really be different code (the -I order took effect)
    d = np.max(np.abs(hA - hB))
    good = d > 0
    ok &= good
    print(f"  [{'ok' if good else 'WRONG'}] A and B impulse responses differ: max|A-B| = {d:.3e}")
    print("SELFTEST", "OK" if ok else "FAILED")
    return ok


# ---- M4: modulation -------------------------------------------------------------------
PRE = 1.0   # pre-roll so si.smoo has settled on f0 (tau = 22.7 ms)
SCEN = {
    "gliss": dict(seconds=125.0, kw={"mode": 1, "f0": 96, "f1": 111, "t0": PRE + 2, "dur": 120, "smoo": 1}),
    "step": dict(seconds=10.0, kw={"mode": 3, "f0": 96, "f1": 111, "t0": PRE + 5, "smoo": 1}),
}


def scen_run(key, scen, sr=48000, prec="double", inp="noise", proc=None, lib=None):
    p, l, _ = CANDS[key] if key in CANDS else (proc, lib, "")
    s = SCEN[scen]
    n = int(s["seconds"] * sr)
    F0 = SLIDER_96 * K1015
    kw = dict(s["kw"], **cparams(key, sr, F0) if key in CANDS else {}, pre=PRE)
    if inp == "noise":
        kw["in"] = "file:" + noise_file(sr, s["seconds"] + PRE)
    else:
        kw["in"] = inp
    return run(proc or p, lib or l, sr, n, prec=prec, **kw)


def window_db(y, sr, win):
    w = int(win * sr)
    k = len(y) // w
    return db(np.sqrt(np.mean(y[: k * w].reshape(k, w) ** 2, axis=1)))


def profile(ys, sr, t_from=4.8, t_to=5.8, win=0.1):
    """100 ms RMS windows around the step (step at 5 s) for each candidate, and B-A re A."""
    w = {k: window_db(y, sr, win) for k, y in ys.items()}
    e = window_db(ys["B"] - ys["A"], sr, win)
    rows = []
    for i in range(int(round(t_from / win)), int(round(t_to / win))):
        rows.append({"t": round(i * win, 3), **{k: float(w[k][i]) for k in ys},
                     "B-A re A": float(e[i] - w["A"][i])})
    return rows


def modulation():
    sr = 48000
    out = {"noise": {}, "leak": {}}
    for scen in SCEN:
        ys = {k: scen_run(k, scen) for k in ORDER}
        a = ys["A"]
        wa = window_db(a, sr, 1.0)
        row = {}
        for key in ORDER:
            y = ys[key]
            r = {"rms_db": float(db(rms(y)))}
            if key == "B":
                e = y - a
                r["B-A rms re A dB"] = float(db(rms(e) / rms(a)))
                r["B-A max re A peak dB"] = float(db(np.max(np.abs(e)) / np.max(np.abs(a))))
                r["B-A worst 1 s window re A dB"] = float(np.max(window_db(e, sr, 1.0) - wa))
            w20 = window_db(y, sr, 0.02)
            r["max 20 ms window dB"] = float(w20.max())
            r["median 20 ms window dB"] = float(np.median(w20))
            if scen == "step":
                i0 = int(5.0 / 0.02)
                after = float(10 * np.log10(np.mean(10 ** (w20[i0 + 50:] / 10))))
                r["max 20 ms in 1 s after step, re later level dB"] = float(w20[i0:i0 + 50].max() - after)
                # the same statistic where nothing happens (2..3 s re 2..5 s): the noise's own crest
                quiet = float(10 * np.log10(np.mean(10 ** (w20[100:250] / 10))))
                r["same, no-step baseline 2-3 s dB"] = float(w20[100:150].max() - quiet)
            row[key] = r
        out["noise"][scen] = row
        if scen == "step":
            out["step_profile"] = {"noise": profile(ys, sr)}
    # in-band tones through the step: the old centre (the filter moves away) and the new one
    for fname, f in (("tone at old F 97.44 Hz", SLIDER_96 * K1015), ("tone at new F 112.665 Hz", SLIDER_111 * K1015)):
        ys = {k: scen_run(k, "step", inp=f"tone:{f}:1") for k in ORDER}
        out["step_profile"][fname] = profile(ys, sr)
    # #262 leakage probe: a unit tone far above the band, heavily attenuated by every candidate
    for scen in SCEN:
        row = {}
        for key in ORDER:
            y = scen_run(key, scen, inp="tone:1000:1")
            row[key] = {"peak dBFS (after 0.5 s)": float(db(np.max(np.abs(y[sr // 2:])))),
                        "rms dBFS (after 0.5 s)": float(db(rms(y[sr // 2:])))}
            if scen == "step":
                i0 = int(5.0 * sr)
                row[key]["rms 1 s before step dBFS"] = float(db(rms(y[i0 - sr:i0])))
                row[key]["peak 1 s after step dBFS"] = float(db(np.max(np.abs(y[i0:i0 + sr]))))
        out["leak"][scen] = row
    return out


# ---- measure -------------------------------------------------------------------------------
M1_CASES = [(48000, SLIDER_96 * K1015), (48000, SLIDER_111 * K1015)]


def measure():
    t0 = time.time()
    R = {"m1": {}, "notes": {}}
    for sr, F in M1_CASES:
        tag = f"{sr}|{F:.4f}"
        hA, tailA = impulse_response("A", sr, F)
        mA = response_metrics(hA, sr, F)
        mA["ir_tail_db"] = tailA
        EDGES[(sr, F)] = (mA["fl"], mA["fu"], "measured")
        fl, fu = analytic_edges(sr, F)
        R["notes"][tag] = {"measured_fl": mA["fl"], "measured_fu": mA["fu"], "analytic_fl": fl, "analytic_fu": fu}
        row = {"A": mA}
        for key in ORDER[1:]:
            h, tail = impulse_response(key, sr, F)
            m = response_metrics(h, sr, F)
            m["ir_tail_db"] = tail
            m["params"] = cparams(key, sr, F)
            row[key] = m
        R["m1"][tag] = row
        print(f"M1 {tag} done ({time.time() - t0:.0f} s)", flush=True)
    R["mod"] = modulation()
    print(f"M4 done ({time.time() - t0:.0f} s)", flush=True)
    R["commands"] = COMMANDS
    with open(os.path.join(WORK, "results.json"), "w") as f:
        json.dump(R, f, indent=1)
    write_results_md(R)
    print(f"measure done ({time.time() - t0:.0f} s)")


# ---- results.md -----------------------------------------------------------------------------
NOTES_MARK = "<!-- hand-written notes below this line are preserved by analyze.py -->"


def f2(x, d=2):
    return "—" if x is None else f"{x:.{d}f}"


def theory_row(sr, F):
    """Analytic |H| (dB, absolute, peak -6.02) at the octave points, bilinear-warped."""
    t = math.tan(math.pi * F / sr)
    fl, fu = analytic_edges(sr, F)
    wl, wu = math.tan(math.pi * fl / sr) / t, math.tan(math.pi * fu / sr) / t
    a, c = [], []
    for o in (-0.25, 0.25, -0.5, 0.5, -1, 1, -2, 2):
        r = math.tan(math.pi * F * 2 ** o / sr) / t
        a.append(-10 * math.log10(2 + r ** 48 + r ** -48))
        x = (r - 1 / r) / (wu - wl)       # Butterworth bandpass prototype frequency
        c.append(-6.0206 - 10 * math.log10(1 + x ** 48))
    return {"A/B (HP24·LP24)": a, "C (Butterworth BP, Nh 24)": c}


def octcell(o, k):
    v = o.get(str(k), o.get(k))
    return f2(v, 1)


def write_results_md(R):
    L = []
    A = L.append
    A("# LMO band filter — results")
    A("")
    A("Generated by `analyze.py measure` from executed renders; the tables are overwritten on every run.")
    A("Commands: `./build.sh all`, then `.venv/bin/python analyze.py measure` from this folder with the seam-ltm venv.")
    A("F is the filter centre, F = 1.015 × slider: 97.44 Hz is slider 96 (cue 1), 112.665 Hz is slider 111 (end of cue 2).")
    A("Sample rate 48 kHz, double precision, for every number below.")
    A("Levels are absolute dB of the filter response: A peaks at −6.02 dB with a rounded top; C is scaled by 0.5 so its maximally flat passband sits at the same −6.02 dB.")
    A("")
    A("Candidates:")
    A("")
    for k in ORDER:
        A(f"- **{k}** — {CANDS[k][2]}")
    A("")
    A("## M1 — magnitude response (impulse response)")
    A("")
    for tag, row in R["m1"].items():
        sr, F = tag.split("|")
        n = R["notes"][tag]
        A(f"### F = {float(F):.3f} Hz")
        A("")
        A(f"A's measured half-power edges (−3.01 dB re its peak): {n['measured_fl']:.4f} / {n['measured_fu']:.4f} Hz; "
          f"analytic, bilinear-warped: {n['analytic_fl']:.4f} / {n['analytic_fu']:.4f} Hz. C is set to the measured edges.")
        A("")
        A("| cand | peak dB | f peak Hz | √(fl·fu) Hz | fl Hz | fu Hz | BW Hz | BW % F | Q eq | −¼ oct | +¼ oct | −½ oct | +½ oct | −1 oct | +1 oct | −2 oct | +2 oct | floor beyond ±2 oct | noise gain Σh² dB |")
        A("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        for k in ORDER:
            m = row[k]
            o = m["oct"]
            A(f"| {k} | {m['peak_db']:.3f} | {m['f_peak']:.3f} | {m['f_centre']:.3f} | {m['fl']:.3f} | {m['fu']:.3f} | {m['bw']:.3f} | "
              f"{m['bw_pct']:.3f} | {m['q_eq']:.2f} | " + " | ".join(octcell(o, x) for x in (-0.25, 0.25, -0.5, 0.5, -1, 1, -2, 2))
              + f" | {m['floor_db']:.1f} | {m['gain_sumsq_db']:.2f} |")
        th = theory_row(int(sr), float(F))
        for name, vals in th.items():
            A(f"| *theory {name}* | −6.021 | | | | | | | | " + " | ".join(f"*{v:.1f}*" for v in vals) + " | | |")
        A("")
        A("Measured cells between about −185 and −215 dB sit on the double-precision floor of the impulse response wherever the theory row is lower; the filters themselves go lower. "
          "C's f peak is the argmax of a maximally flat top (flat to well under 0.01 dB), so it carries no information; √(fl·fu) is C's centre.")
        A("")
        A("Deviation from the analytic |H|² = 1/(2 + r⁴⁸ + r⁻⁴⁸) (warped r, wherever the theory is above −150 dB): "
          + ", ".join(f"{k} {row[k]['dev_hplp_theory_db']:.2e} dB" for k in ("A", "B")) + ".")
        A(f"IR length check: energy in the last 10 % of each impulse response re total: "
          + ", ".join(f"{k} {row[k]['ir_tail_db']:.0f} dB" for k in ORDER) + ".")
        A("")
    A("## M2 — time behaviour (same impulse responses)")
    A("")
    A("Group delay at F; time of the Hilbert-envelope peak; last time the envelope is above −60 dB re its peak; "
      "Schroeder energy-decay curve reaching −20 and −60 dB. All in ms from the impulse.")
    A("")
    for tag, row in R["m1"].items():
        sr, F = tag.split("|")
        A(f"**F = {float(F):.3f} Hz**")
        A("")
        A("| cand | group delay at F | envelope peak | envelope −60 dB | EDC −20 dB | EDC −60 dB |")
        A("|---|---|---|---|---|---|")
        for k in ORDER:
            m = row[k]
            A(f"| {k} | {m['gd_ms']:.1f} | {m['t_envpeak_ms']:.1f} | {m['t_env60_ms']:.1f} | {m['t_edc20_ms']:.1f} | {m['t_edc60_ms']:.1f} |")
        A("")
    A("## M4 — modulation")
    A("")
    A("Input: no.multinoise stream 0; a 1 s pre-roll at slider 96 is discarded so si.smoo has settled (τ = 22.7 ms). "
      "**gliss** (primary): slider 96 → 111 linear over 120 s starting at 2 s, through si.smoo, 125 s in all. "
      "**step** (secondary): slider 96 → 111 at 5 s, through si.smoo, 10 s. "
      "C keeps the edge ratios measured at F = 97.44 Hz while F moves (constant relative bandwidth, as A). "
      "Expected level change for constant relative bandwidth: 10·log10(111/96) = +0.63 dB.")
    A("")
    for scen, row in R["mod"]["noise"].items():
        A(f"**{scen}, noise input**")
        A("")
        cols = []
        for k in ORDER:
            for c in row[k]:
                if c not in cols:
                    cols.append(c)
        A("| cand | " + " | ".join(cols) + " |")
        A("|---|" + "---|" * len(cols))
        for k in ORDER:
            A(f"| {k} | " + " | ".join(f2(row[k].get(c), 2) for c in cols) + " |")
        A("")
    A("**step, time profile** — RMS of 100 ms windows (dBFS; window t covers t … t+0.1 s), step at 5.0 s. "
      "B−A re A is the difference signal in the same window relative to A's level.")
    A("")
    for name, rows in R["mod"]["step_profile"].items():
        A(f"*{name} input*")
        A("")
        A("| t s | " + " | ".join(ORDER) + " | B−A re A |")
        A("|---|" + "---|" * (len(ORDER) + 1))
        for r in rows:
            A(f"| {r['t']:.1f} | " + " | ".join(f"{r[k]:.2f}" for k in ORDER) + f" | {r['B-A re A']:.1f} |")
        A("")
    A("**#262 leakage probe** — a unit-amplitude 1 kHz tone (more than 3 octaves above the band) through the same scenarios; dBFS of the output.")
    A("")
    for scen, row in R["mod"]["leak"].items():
        A(f"*{scen}*")
        A("")
        cols = list(row["A"].keys())
        A("| cand | " + " | ".join(cols) + " |")
        A("|---|" + "---|" * len(cols))
        for k in ORDER:
            A(f"| {k} | " + " | ".join(f"{row[k][c]:.1f}" for c in cols) + " |")
        A("")
    A("## Commands run")
    A("")
    A(f"{len(R['commands'])} harness invocations; one of each kind (paths relative to this folder):")
    A("")
    A("```")
    seen = set()
    for cmd in R["commands"]:
        parts = cmd.split()
        key = parts[0] + " " + " ".join(sorted(a.split("=")[0] + ("=" + a.split("=")[1][:5] if a.startswith(("in=", "mode=")) else "") for a in parts[3:]))
        if key not in seen:
            seen.add(key)
            A(cmd)
    A("```")
    A("")
    old = open(RESULTS_MD).read() if os.path.exists(RESULTS_MD) else ""
    notes = old.split(NOTES_MARK, 1)[1] if NOTES_MARK in old else "\n"
    with open(RESULTS_MD, "w") as f:
        f.write("\n".join(L) + "\n" + NOTES_MARK + notes)


# ---- renders --------------------------------------------------------------------------------
TARGET_DBFS = -20.0
FADE = 0.05
RENDER_SCEN = {
    # name: (seconds, harness kw, pre-roll)
    "steady96": (20.0, {"mode": 0, "smoo": 0, "f0": 96}, 0.0),
    "gliss": (125.0, SCEN["gliss"]["kw"], PRE),
    "step": (10.0, SCEN["step"]["kw"], PRE),
}
RENDER_CANDS = ["A", "B", "C"]


def fade(y, sr):
    n = int(FADE * sr)
    w = 0.5 - 0.5 * np.cos(np.pi * np.arange(n) / n)
    y = y.copy()
    y[:n] *= w
    y[-n:] *= w[::-1]
    return y


def write_wav(path, y, sr):
    peak = float(np.max(np.abs(y)))
    if peak >= 1.0:
        raise RuntimeError(f"{path} would clip (peak {peak:.3f})")
    sf.write(path, y, sr, subtype="PCM_24")
    return peak


def render():
    sr = 48000
    F0 = SLIDER_96 * K1015
    os.makedirs(os.path.join(RENDERS, "blind"), exist_ok=True)
    log = []
    made = {}
    for scen, (sec, kw, pre) in RENDER_SCEN.items():
        n = int(sec * sr)
        for key in RENDER_CANDS:
            proc, lib, _ = CANDS[key]
            y = run(proc, lib, sr, n, **kw, **cparams(key, sr, F0), pre=pre,
                    **{"in": "file:" + noise_file(sr, sec + pre)})
            y = fade(y, sr)
            g = 10 ** (TARGET_DBFS / 20) / rms(y)
            y = y * g
            name = f"{scen}_{key.replace(chr(39), 'p')}.wav"
            pk = write_wav(os.path.join(RENDERS, name), y, sr)
            made[(scen, key)] = name
            log.append(f"| {name} | {TARGET_DBFS:.1f} | {20 * math.log10(g):+.2f} | {20 * math.log10(pk):.2f} |")
    # Davide's true level, unmatched
    sec = 20.0
    y = run("davide", "old", sr, int(sec * sr), mode=0, smoo=0, f0=96, **{"in": "file:" + noise_file(sr, sec)})
    y = fade(y, sr)
    pk = write_wav(os.path.join(RENDERS, "steady96_A_davide_level.wav"), y, sr)
    log.append(f"| steady96_A_davide_level.wav | {20 * math.log10(rms(y)):.2f} | unmatched | {20 * math.log10(pk):.2f} |")
    # blind set: per scenario, shuffled letters
    rng = random.SystemRandom()
    key_lines = ["# Blind key -- do not open before listening.", ""]
    for scen in RENDER_SCEN:
        letters = list("XYZ")[: len(RENDER_CANDS)]
        rng.shuffle(letters)
        for key, let in zip(RENDER_CANDS, letters):
            src = os.path.join(RENDERS, made[(scen, key)])
            dst = os.path.join(RENDERS, "blind", f"{scen}_{let}.wav")
            data, _ = sf.read(src, dtype="int32")
            sf.write(dst, data, sr, subtype="PCM_24")
            key_lines.append(f"{scen}_{let}.wav = {key}  ({CANDS[key][2]})")
        key_lines.append("")
    with open(os.path.join(RENDERS, "blind", "KEY.txt"), "w") as f:
        f.write("\n".join(key_lines))
    with open(os.path.join(RENDERS, "render-log.md"), "w") as f:
        f.write("| file | RMS dBFS | gain applied dB | peak dBFS |\n|---|---|---|---|\n" + "\n".join(log) + "\n")
    print("\n".join(log))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    if cmd in ("selftest", "all"):
        if not selftest():
            sys.exit(1)
    if cmd in ("measure", "all"):
        measure()
    if cmd in ("render", "all"):
        render()
