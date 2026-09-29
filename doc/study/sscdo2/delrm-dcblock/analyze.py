#!/usr/bin/env python3
"""analyze.py -- delRM block 5: what the original's DC blockers removed, and what they did to the note.

usage (from this folder):  ./run.sh [check|measure|render|all] [SOURCE.wav]

Everything runs at 96 kHz, the rate SSCDO#2 is played at. 48 kHz appears once, in the
check, only to show that fi.dcblocker moves with the rate and fi.dcblockerat does not.

  check    the probes are what the README says they are (each with a mutation that must fail)
  measure  DC and low band before and after the DC blockers -> results.md
  render   the A/B for Davide: our chain clean, and with the two DC blockers of the performance
"""
import math, os, subprocess, sys
import numpy as np
import soundfile as sf
from scipy import signal

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "build", "bin")
WORK = os.path.join(HERE, "build", "work")
RENDERS = os.path.join(HERE, "renders")
os.makedirs(WORK, exist_ok=True)
SRC = sys.argv[2] if len(sys.argv) > 2 else "/Volumes/MDAGIFT/CCB_petalonio_oriz_DO.wav"

SR = 96000
MT = 7.291              # Davide's initial 22 ms
P = 0.995               # fi.dcblocker's pole
WN = (1 - P) / (1 + P)  # pi*fb/SR of the fi.dcblockerat with the same pole
FB96 = WN * 96000 / math.pi
DC = 3.22e-6            # the mean of track 1, measured: the offset of the real chain
FLOOR_DBFS = -70.0      # microphone and preamp floor while nobody plays
PRE_NOTE_S = 300        # five minutes of floor before the note
F0 = 29.7               # the note's fundamental


def run(probe, sr, n, inp, pre=0.0, **kw):
    out = os.path.join(WORK, f"y_{os.getpid()}.f64")
    args = [os.path.join(BIN, probe), str(sr), str(int(n)), f"out={out}", f"pre={pre}", f"in={inp}"]
    if probe not in ("dry", "dcb", "dcbat", "dcbatx"):   # the probes without a delay have no mt
        args.append(f"mt={MT}")
    args += [f"{k}={v}" for k, v in kw.items()]
    subprocess.run(args, check=True, capture_output=True)
    y = np.fromfile(out)
    os.remove(out)
    return y


def note():
    x, fs = sf.read(SRC, dtype="float64")
    assert fs == SR, f"the recording is at {fs} Hz, expected {SR}"
    x = x[:, 0]
    return x[: np.nonzero(x)[0][-1] + 1]


def f32(name, x):
    p = os.path.join(WORK, name)
    np.asarray(x, dtype=np.float32).tofile(p)
    return "file:" + p


def inputs():
    x = note()
    rng = np.random.default_rng(2)
    floor = rng.normal(0, 10 ** (FLOOR_DBFS / 20), PRE_NOTE_S * SR) + DC
    return {"fresh": (f32("note.f32", x), len(x), 0.0),
            "late": (f32("late.f32", np.concatenate([floor, x])), len(x), PRE_NOTE_S)}


def dcb(y):
    return signal.lfilter([1, -1], [1, -P], y)


def rms_db(y):
    return 20 * math.log10(max(math.sqrt(float(np.mean(y ** 2))), 1e-300))


def band_db(y, lo, hi):
    f, S = signal.welch(y, SR, nperseg=1 << 17)
    m = (f >= lo) & (f < hi)
    return 10 * math.log10(max(float(S[m].sum() * (f[1] - f[0])), 1e-300))


def hp_db(f, sr=SR):
    z = np.exp(1j * 2 * np.pi * f / sr)
    return 20 * math.log10(abs((1 - 1 / z) / (1 - P / z)))


def at_db(h, f, sr):
    H = np.fft.rfft(h, 1 << 22)
    k = int(round(f / (sr / 2) * (len(H) - 1)))
    return 20 * math.log10(abs(H[k]))


# ---- check ------------------------------------------------------------------------------
def check():
    ok = True
    lines = []

    def claim(name, passed, detail):
        nonlocal ok
        ok &= passed
        lines.append(f"| {name} | {'PASS' if passed else 'FAIL'} | {detail} |")

    b0 = 1 / (1 + WN)
    h, ha, hx = (run(p, SR, SR, "impulse") for p in ("dcb", "dcbat", "dcbatx"))
    d = float(np.max(np.abs(h - ha / b0)))
    claim("fi.dcblocker = fi.dcblockerat(76.59)/b0 at 96 kHz", d < 1e-12, f"max diff {d:.1e}, 1/b0 = {1 / b0:.6f} ({20 * math.log10(1 / b0):+.4f} dB)")
    bx = 1 / (1 + math.pi * 76 / SR)
    d = float(np.max(np.abs(h - hx / bx)))
    claim("mutation: fi.dcblockerat(76) must differ", d > 1e-6, f"max diff {d:.1e}")

    h48, ha48 = run("dcb", 48000, 48000, "impulse"), run("dcbat", 48000, 48000, "impulse")
    claim("fi.dcblockerat(76.59) at 76.59 Hz: -3.01 dB at 96 and at 48 kHz",
          abs(at_db(ha, FB96, SR) + 3.0103) < 0.01 and abs(at_db(ha48, FB96, 48000) + 3.0103) < 0.01,
          f"{at_db(ha, FB96, SR):.3f} / {at_db(ha48, FB96, 48000):.3f} dB")
    claim("fi.dcblocker at 76.59 Hz moves with the rate", abs(at_db(h, FB96, SR) - at_db(h48, FB96, 48000)) > 1,
          f"{at_db(h, FB96, SR):.3f} dB at 96 kHz, {at_db(h48, FB96, 48000):.3f} dB at 48 kHz")

    i = inputs()["fresh"]
    for base, hp in (("comb", "combhp"), ("trip", "triphp")):
        y, yh = run(base, SR, i[1], i[0]), run(hp, SR, i[1], i[0])
        d2, d1 = float(np.max(np.abs(dcb(dcb(y)) - yh))), float(np.max(np.abs(dcb(y) - yh)))
        claim(f"{hp} = {base} through two fi.dcblocker", d2 < 1e-12, f"max diff {d2:.1e}")
        claim(f"mutation: {base} through one must differ", d1 > 1e-4, f"max diff {d1:.1e}")

    print("| claim | result | detail |\n|---|---|---|\n" + "\n".join(lines))
    return ok, lines


# ---- measure ----------------------------------------------------------------------------
def measure(check_lines):
    i = inputs()
    out = []

    rows = [("channels 2/4, original integrator, fresh", "triporig", "fresh"),
            ("channels 2/4, original integrator, after 5 min", "triporig", "late"),
            ("channels 2/4, leaky integrator (ours), fresh", "trip", "fresh"),
            ("channels 2/4, leaky integrator (ours), after 5 min", "trip", "late"),
            ("channels 1/3, comb", "comb", "fresh"),
            ("input, the dry note", "dry", "fresh")]
    out.append("## What reaches the DC blockers\n")
    out.append("The mean of each channel at the point where the original puts its first `fi.dcblocker`, on the note (3.56 s) "
               "and on the same note after 300 s of microphone floor with the real chain's DC (3.22e-6).\n")
    out.append("| signal | mean | DC re RMS |\n|---|---|---|")
    for name, probe, k in rows:
        y = run(probe, SR, i[k][1], i[k][0], pre=i[k][2])
        mu = float(np.mean(y))
        out.append(f"| {name} | {mu:+.2e} | {20 * math.log10(abs(mu)) - rms_db(y):.1f} dB |")

    out.append("\n## What they do to the note\n")
    out.append("0, 1 and 2 `fi.dcblocker` in series (block 5, then block 6), leaky integrator.\n")
    out.append("| channel | RMS 0 / 1 / 2 (dBFS) | 25-35 Hz band 0 / 1 / 2 (dB) | below 20 Hz, 0 (dB) |\n|---|---|---|---|")
    for name, probe in (("1/3, comb", "comb"), ("2/4, triple product + compressor", "trip")):
        y = run(probe, SR, i["fresh"][1], i["fresh"][0])
        ys = (y, dcb(y), dcb(dcb(y)))
        out.append(f"| {name} | {' / '.join(f'{rms_db(v):.2f}' for v in ys)} | "
                   f"{' / '.join(f'{band_db(v, 25, 35):.1f}' for v in ys)} | {band_db(y, 0, 20):.1f} |")

    out.append("\nTheir response on the note's odd partials, at 96 kHz (computed from the pole):\n")
    out.append("| partial | 1 DC blocker | 2 DC blockers |\n|---|---|---|")
    for h in (1, 3, 5, 7, 9):
        g = hp_db(h * F0)
        out.append(f"| {h * F0:.1f} Hz ({h}) | {g:.2f} dB | {2 * g:.2f} dB |")

    md = ("# delRM block 5: results\n\nGenerated by `analyze.py`; 96 kHz, mt = 7.291 m, track 1 of "
          "`CCB_petalonio_oriz_DO.wav`.\n\n## Check\n\n| claim | result | detail |\n|---|---|---|\n"
          + "\n".join(check_lines) + "\n\n" + "\n".join(out) + "\n")
    with open(os.path.join(HERE, "results.md"), "w") as fh:
        fh.write(md)
    print("\n".join(out))


# ---- render -----------------------------------------------------------------------------
def render():
    i = inputs()["fresh"]
    n = int(0.02 * SR)
    w = 0.5 - 0.5 * np.cos(np.pi * np.arange(n) / n)

    def faded(y):
        y = y.copy(); y[:n] *= w; y[-n:] *= w[::-1]
        return y

    files = [("note_dry.wav", "dry"), ("ch1_comb_clean.wav", "comb"), ("ch1_comb_2dcblockers.wav", "combhp"),
             ("ch2_triple_clean.wav", "trip"), ("ch2_triple_2dcblockers.wav", "triphp")]
    ys = {name: faded(run(probe, SR, i[1], i[0])) for name, probe in files}
    crest = {k: 20 * math.log10(np.max(np.abs(y))) - rms_db(y) for k, y in ys.items()}
    target = min(-20.0, math.floor(-1.0 - max(crest.values())))   # one RMS for all, the widest crest peaks at -1 dBFS
    log = []
    for name, _ in files:
        g = 10 ** (target / 20) / 10 ** (rms_db(ys[name]) / 20)
        y = ys[name] * g
        assert np.max(np.abs(y)) < 1, name
        sf.write(os.path.join(RENDERS, name), y, SR, subtype="PCM_24")
        log.append(f"| {name} | {20 * math.log10(g):+.2f} | {crest[name]:.1f} |")
    with open(os.path.join(RENDERS, "render-log.md"), "w") as fh:
        fh.write(f"96 kHz, 24-bit, mt = 7.291 m, leaky integrator; every file normalised to {target:.0f} dBFS RMS (20 ms fades), "
                 "the level at which the widest crest factor peaks below -1 dBFS. "
                 "The A/B compares timbre, not level; the difference of the gain column within a pair is the level "
                 "the two DC blockers took away.\n\n"
                 "| file | gain dB | crest factor dB |\n|---|---|---|\n" + "\n".join(log) + "\n")
    print("\n".join(log))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    passed, lines = check()
    if not passed:
        sys.exit(1)
    if cmd in ("measure", "all"):
        measure(lines)
    if cmd in ("render", "all"):
        render()
