#!/usr/bin/env python3
"""Reverberation time from a recorded `ltglide` pass (ISO 3382 method).

Written so that a STONE calibration can state where its own measurement stops
describing the loudspeaker and starts describing the room. The Schroeder
frequency needs T60 and a volume; this reads T60 out of a pass we already play.

WHY ltglide RATHER THAN A SWEEP
-------------------------------
`ltglide` does not emit a continuous glissando. It emits Linkwitz bursts --
kN = 5 cycles of a sine under a Hann window -- one per frequency, each followed
by `delta` seconds of silence (gap mode). Every burst is therefore its own
interrupted-excitation experiment in the sense of ISO 3382-2, and the room
decays alone in the gap that follows it.

The consequence is that a pass does not yield *a* T60. It yields T60(f), one
estimate per burst. Below the Schroeder frequency that distinction is the whole
point: decay there is individual modes ringing, not a room average, and a
single number hides exactly the structure worth seeing.

A 5-cycle Hann burst has a main lobe about one octave wide (half-width 2f/kN),
so each estimate carries roughly the octave-band resolution ISO 3382 asks for.
It is not a pure tone and this tool does not pretend otherwise.

THE PASS TIMELINE, AND WHY THE SCHEDULE IS RECONSTRUCTED
-------------------------------------------------------
From `plugins/ltglide/source/ltglide_dsp.h` (GlideTransport):

    Dirac | Lead 5.0 s | Glide T s | Tail 5.0 s | Dirac

The head Dirac is a one-sample impulse, so the glide begins exactly
kLeadSec = 5.0 s after it. That is enough to *reconstruct* every burst onset
instead of hunting for it, which is what the ltglide receiver does with the
same reasoning: the reference is deterministic, so regenerate it.

    t_0 = 0
    f_k = f0 + (f1-f0)*p        (smode linear)
          f0 * (f1/f0)**p       (smode exponential), p = t_k / T
    period_k = kN/f_k + delta   (dmode gap)
               max(delta, kN/f_k)  (dmode step)
    t_{k+1} = t_k + period_k

The burst itself occupies the first kN/f_k seconds; the decay window is the
rest of the period.

The 5 s of silence after the head Dirac is also a free integrated-impulse-
response measurement -- the ISO 3382 reference method -- and is analysed as a
cross-check. Expect it to be noisy: one sample of impulse carries very little
energy next to a burst, so the bursts are the working estimate and the Dirac is
the second opinion.

WHAT IS MEASURED, AND WHAT IS NOT
---------------------------------
T60 is a property of the room: how fast energy leaves it. It does not depend on
the source spectrum, so the amplifier EQ does not change it -- it only changes
how far each burst starts above the noise floor. Measure with the EQ bypassed
and one ltglide level works across the band; measure through a calibration
preset with 27 dB of tilt in it and no single level does.

The EQ's own ringing is not a confound: a Q=2 peak at 198 Hz decays in about
3 ms against a room's 1-2 s.

METHOD
------
Per burst: band-pass (octave, 4th-order Butterworth, zero-phase), estimate the
noise floor from the tail of the gap, truncate the integration where the decay
reaches noise + kTruncMarginDb (a simplified Lundeby), integrate backwards
(Schroeder), then fit a line over -5..-25 dB and -5..-35 dB. Following ISO 3382, T20 and T30
NAME the reverberation time already extrapolated to a 60 dB decay from those
ranges -- they are T60 estimates, not the time to fall 20 or 30 dB. The
standard asks for the fit quality, so the non-linearity of each fit is
reported and bad fits are flagged rather than silently averaged.

USAGE
-----
    .venv/bin/python tools/decay-from-glide.py REC.wav \\
        --t 120 --f0 315 --f1 40 --delta 2.0 [--volume 96]

Requires numpy, scipy, soundfile (see requirements.txt).
"""

from __future__ import annotations

import argparse
import math
import sys

try:
    import numpy as np
    import soundfile as sf
    from scipy.signal import butter, sosfiltfilt
except ImportError as exc:  # pragma: no cover - environment guard
    sys.exit(
        f"missing dependency: {exc.name}\n"
        "create the venv and install:\n"
        "    python3 -m venv .venv && .venv/bin/pip install -r requirements.txt"
    )

# --- constants that mirror ltglide_dsp.h -----------------------------------
kN = 5                  # GlissBurst::kN, canonical Linkwitz cycle count
kFloorHz = 20.0         # GlissBurst::kFloorHz
kLeadSec = 5.0          # GlideTransport::kLeadSec
kTailSec = 5.0          # GlideTransport::kTailSec

# --- analysis constants ----------------------------------------------------
kTruncMarginDb = 10.0   # integrate down to noise + this, then stop
kFitStartDb = -5.0      # ISO 3382: skip the direct sound
kEdgeGuardSec = 0.02    # discard this much after filtering, per segment edge
kMinUsableDb = 25.0     # below this usable range, not even T20 is honest
kMaxNonlinearity = 10.0  # ISO 3382 curvature figure above which a fit is suspect

ISO_OCTAVE_CENTRES = [31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0]


# ---------------------------------------------------------------------------
# schedule reconstruction
# ---------------------------------------------------------------------------

def sweep_freq(f0: float, f1: float, smode: str, p: float) -> float:
    """SweepFreq::at() from ltglide_dsp.h."""
    if smode == "lin":
        return f0 + (f1 - f0) * p
    return f0 * math.pow(f1 / f0, p)


def burst_schedule(f0, f1, t_total, delta, smode, dmode):
    """Reconstruct (onset, frequency, period) for every grain of a pass.

    Mirrors GlissBurst's retriggered-grain engine: the frequency is latched at
    onset and held for the whole grain, and the grain's period is derived from
    that held frequency -- not from the continuously swept one.
    """
    out = []
    t = 0.0
    while t < t_total:
        p = t / t_total
        f = sweep_freq(f0, f1, smode, p)
        den = max(kFloorHz, f)
        period = (max(delta, kN / den) if dmode == "step" else kN / den + delta)
        out.append((t, f, period))
        t += period
    return out


# ---------------------------------------------------------------------------
# locating the pass in the recording
# ---------------------------------------------------------------------------

def find_glide_start(x, fs, t_total, frame_sec=0.01):
    """Return (glide_start_sec, head_dirac_sec, diagnostics).

    The head Dirac is the first transient of the pass, and the glide begins
    kLeadSec after it. Detection is deliberately crude -- a frame-energy
    threshold over the noise floor -- because the timeline does the real work
    and a wrong answer here is visible: the reported span must come out as
    kLeadSec + T + kTailSec.
    """
    hop = max(1, int(round(frame_sec * fs)))
    n_frames = len(x) // hop
    if n_frames < 10:
        raise ValueError("recording too short to analyse")
    frames = x[: n_frames * hop].reshape(n_frames, hop)
    rms = np.sqrt(np.mean(frames.astype(np.float64) ** 2, axis=1))

    floor = np.percentile(rms[rms > 0], 10) if np.any(rms > 0) else 0.0
    if floor <= 0:
        floor = np.max(rms) * 1e-6 + 1e-12
    thresh = floor * 10.0  # +20 dB over the floor

    above = rms > thresh
    if not np.any(above):
        raise ValueError("no signal found above the noise floor")
    idx = np.flatnonzero(above)
    first, last = idx[0], idx[-1]

    head = first * hop / fs
    span = (last - first) * hop / fs
    expected = kLeadSec + t_total + kTailSec
    diag = {
        "head_dirac_sec": head,
        "span_sec": span,
        "expected_span_sec": expected,
        "span_error_pct": 100.0 * (span - expected) / expected,
        "noise_floor_rms": float(floor),
    }
    return head + kLeadSec, head, diag


# ---------------------------------------------------------------------------
# decay analysis
# ---------------------------------------------------------------------------

def octave_sos(fc, fs, order=4):
    """Zero-phase-ready octave band around fc, clamped to a valid Nyquist."""
    lo = fc / math.sqrt(2.0)
    hi = fc * math.sqrt(2.0)
    nyq = fs / 2.0
    hi = min(hi, nyq * 0.98)
    lo = max(lo, 1.0)
    if lo >= hi:
        return None
    return butter(order, [lo / nyq, hi / nyq], btype="band", output="sos")


def schroeder_decay(y, fs):
    """Backward-integrated decay curve in dB, truncated at the noise floor.

    Returns (times, level_db, usable_db, noise_db). The truncation is the
    simplified Lundeby step: the integral is only honest while the signal is
    above the noise, so it stops at noise + kTruncMarginDb rather than running
    into the floor and bending the tail upward.
    """
    p = y.astype(np.float64) ** 2
    if len(p) < 32:
        return None

    tail = p[int(len(p) * 0.8):]
    noise = float(np.mean(tail)) if len(tail) else 0.0

    # smooth to find where the decay meets the floor
    win = max(1, int(round(0.01 * fs)))
    kernel = np.ones(win) / win
    smooth = np.convolve(p, kernel, mode="same")

    limit = noise * (10.0 ** (kTruncMarginDb / 10.0))
    above = np.flatnonzero(smooth > limit)
    trunc = int(above[-1]) + 1 if len(above) else len(p)
    trunc = max(trunc, 32)
    trunc = min(trunc, len(p))

    e = np.cumsum(p[:trunc][::-1])[::-1]
    if e[0] <= 0:
        return None
    level = 10.0 * np.log10(e / e[0] + 1e-300)
    times = np.arange(trunc) / fs

    # Decay range actually available: the band-passed signal power at the
    # start of the decay, over the noise power in the same window. Using
    # -level[-1] instead would read the last retained SAMPLE against the whole
    # integral and report ranges the noise floor cannot support.
    head = p[: max(1, int(0.05 * trunc))]
    head_pow = float(np.mean(head))
    usable = (10.0 * math.log10(head_pow / noise)) if (noise > 0 and head_pow > 0) else float("inf")
    noise_db = 10.0 * np.log10(noise / e[0] + 1e-300) if noise > 0 else -np.inf
    return times, level, usable, noise_db


def fit_decay(times, level, top_db, bottom_db):
    """Least-squares slope over [top_db, bottom_db]; returns (T60, nonlinearity).

    `nonlinearity` is 1000*(1-r^2), the ISO 3382 curvature figure: 0 is a
    perfect straight line, and the standard treats values above ~10 as
    suspect.
    """
    sel = (level <= top_db) & (level >= bottom_db)
    if np.count_nonzero(sel) < 16:
        return None, None
    t = times[sel]
    l = level[sel]
    slope, intercept = np.polyfit(t, l, 1)
    if slope >= 0:
        return None, None
    t60 = -60.0 / slope
    pred = slope * t + intercept
    ss_res = float(np.sum((l - pred) ** 2))
    ss_tot = float(np.sum((l - np.mean(l)) ** 2))
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0
    return t60, 1000.0 * (1.0 - r2)


def analyse_segment(x, fs, start_sec, end_sec, fc):
    """Band-pass a decay window and return its T20/T30 estimates."""
    sos = octave_sos(fc, fs, order=4)
    if sos is None:
        return None

    guard = int(round(kEdgeGuardSec * fs))
    a = max(0, int(round(start_sec * fs)) - guard)
    b = min(len(x), int(round(end_sec * fs)) + guard)
    if b - a < 64:
        return None
    seg = sosfiltfilt(sos, x[a:b].astype(np.float64))

    # analyse from the requested start, past the filter's edge transient
    off = int(round(start_sec * fs)) - a + guard
    off = min(off, len(seg) - 1)
    decay = seg[off:max(off + 1, len(seg) - guard)]

    res = schroeder_decay(decay, fs)
    if res is None:
        return None
    times, level, usable, noise_db = res

    t20, nl20 = fit_decay(times, level, kFitStartDb, kFitStartDb - 20.0)
    t30, nl30 = fit_decay(times, level, kFitStartDb, kFitStartDb - 30.0)
    return {
        "usable_db": usable,
        "noise_db": noise_db,
        "t20": t20, "nl20": nl20,
        "t30": t30, "nl30": nl30,
    }


def analyse_dirac(x, fs, head_sec):
    """T60 per ISO octave band from the head Dirac's own decay.

    The pass opens with a one-sample impulse followed by kLeadSec of silence,
    which is an integrated-impulse-response measurement in the ISO 3382 sense
    -- the reference method, obtained for free from the timeline.

    It is the second opinion, not the working estimate: one sample of impulse
    carries far less energy than a burst, so expect most bands to fall below
    the noise and be reported as such. Where it does resolve, it should agree
    with the bursts, and disagreement is worth more than either number alone.
    """
    out = []
    for fc in ISO_OCTAVE_CENTRES:
        r = analyse_segment(x, fs, head_sec, head_sec + kLeadSec, fc)
        if r is None or r["usable_db"] < kMinUsableDb:
            out.append((fc, None, r["usable_db"] if r else None, "noise"))
            continue
        # The ISO 3382 curvature criterion decides here too. Without it a
        # single impulse in a low band fits a straight line through almost
        # nothing and reports tens of seconds with a straight face.
        use30 = r["t30"] is not None
        t60 = r["t30"] if use30 else r["t20"]
        nl = r["nl30"] if use30 else r["nl20"]
        if t60 is None or nl is None or nl > kMaxNonlinearity:
            out.append((fc, None, r["usable_db"], "curved"))
            continue
        out.append((fc, t60, r["usable_db"], "ok"))
    return out


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------

def schroeder_frequency(t60, volume):
    return 2000.0 * math.sqrt(t60 / volume)


def flag_for(r, delta):
    if r is None:
        return "no fit"
    if r["usable_db"] < kMinUsableDb:
        return f"low SNR ({r['usable_db']:.0f} dB)"
    if r["t20"] is None and r["t30"] is None:
        return "no fit"
    # A T30 needs 35 dB of decay inside the gap, i.e. gap > T60*35/60. Past
    # that the gap cannot hold the fit and the estimate leans on truncation.
    est60 = r["t30"] if r["t30"] is not None else r["t20"]
    if est60 > delta / 0.583:
        return "gap too short"
    nl = r["nl30"] if r["t30"] is not None else r["nl20"]
    if nl is not None and nl > kMaxNonlinearity:
        return f"curved ({nl:.0f})"
    return "ok"


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Reverberation time from a recorded ltglide pass.")
    ap.add_argument("recording", help="WAV of the pass, recorded at the mic position")
    ap.add_argument("--t", type=float, required=True, help="ltglide T, sweep seconds")
    ap.add_argument("--f0", type=float, required=True, help="ltglide F0 in Hz")
    ap.add_argument("--f1", type=float, required=True, help="ltglide F1 in Hz")
    ap.add_argument("--delta", type=float, required=True, help="ltglide DELTA in seconds")
    ap.add_argument("--smode", choices=["lin", "exp"], default="exp")
    ap.add_argument("--dmode", choices=["step", "gap"], default="gap")
    ap.add_argument("--channel", type=int, default=0)
    ap.add_argument("--volume", type=float, default=None,
                    help="room volume in m^3; prints the Schroeder frequency")
    ap.add_argument("--glide-start", type=float, default=None,
                    help="override detection: seconds from file start to glide")
    ap.add_argument("--dat", default=None, help="also write a pgfplots table here")
    args = ap.parse_args(argv)

    data, fs = sf.read(args.recording, always_2d=True, dtype="float64")
    x = data[:, min(args.channel, data.shape[1] - 1)]
    print(f"file       {args.recording}")
    print(f"           {len(x)/fs:.2f} s, {fs:g} Hz, channel {args.channel} of {data.shape[1]}")

    if args.glide_start is not None:
        glide_start, head = args.glide_start, args.glide_start - kLeadSec
        print(f"glide      starts at {glide_start:.3f} s (given)")
    else:
        glide_start, head, diag = find_glide_start(x, fs, args.t)
        print(f"head Dirac {head:.3f} s")
        print(f"glide      starts at {glide_start:.3f} s "
              f"(= Dirac + {kLeadSec:g} s lead)")
        print(f"span       {diag['span_sec']:.2f} s measured vs "
              f"{diag['expected_span_sec']:.2f} s expected "
              f"({diag['span_error_pct']:+.1f}%)")
        if abs(diag["span_error_pct"]) > 5.0:
            print("  WARNING: the pass does not match the declared T/delta. "
                  "Check the parameters, or pass --glide-start.")

    sched = burst_schedule(args.f0, args.f1, args.t, args.delta, args.smode, args.dmode)
    print(f"schedule   {len(sched)} bursts reconstructed, "
          f"{args.f0:g} -> {args.f1:g} Hz, {args.dmode} timing\n")

    rows = []
    for onset, f, period in sched:
        burst_end = glide_start + onset + kN / max(kFloorHz, f)
        window_end = glide_start + onset + period
        if window_end * fs > len(x):
            break
        r = analyse_segment(x, fs, burst_end, window_end, f)
        rows.append((f, r))

    print(f"{'freq':>9}  {'T20':>7} {'T30':>7}  {'nonlin':>7}  {'SNR':>6}  flag")
    print(f"{'Hz':>9}  {'s':>7} {'s':>7}  {'':>7}  {'dB':>6}"
          f"      (T20/T30 already extrapolated to 60 dB)")
    good = []
    for f, r in rows:
        if r is None:
            print(f"{f:9.1f}  {'-':>7} {'-':>7}  {'-':>7}  {'-':>6}  no fit")
            continue
        t20 = f"{r['t20']:.3f}" if r["t20"] else "-"
        t30 = f"{r['t30']:.3f}" if r["t30"] else "-"
        nl = r["nl30"] if r["t30"] is not None else r["nl20"]
        nls = f"{nl:.0f}" if nl is not None else "-"
        flag = flag_for(r, args.delta)
        print(f"{f:9.1f}  {t20:>7} {t30:>7}  {nls:>7}  "
              f"{r['usable_db']:6.1f}  {flag}")
        if flag == "ok":
            good.append((f, r["t30"] if r["t30"] else r["t20"]))

    if not good:
        print("\nNo usable estimate. Raise the ltglide level, bypass the "
              "amplifier EQ, or lengthen DELTA.")
        return 1

    print(f"\n{len(good)} of {len(rows)} bursts usable.\n")

    # per ISO octave band
    print("per octave band")
    band_means = {}
    for fc in ISO_OCTAVE_CENTRES:
        lo, hi = fc / math.sqrt(2.0), fc * math.sqrt(2.0)
        vals = [v for f, v in good if lo <= f < hi]
        if vals:
            band_means[fc] = float(np.mean(vals))
            print(f"  {fc:7.1f} Hz   T60 = {np.mean(vals):.2f} s   "
                  f"({len(vals)} bursts, spread {np.ptp(vals):.2f} s)")

    print("\ncross-check from the head Dirac (ISO 3382 impulse-response method)")
    dirac = analyse_dirac(x, fs, head)
    any_dirac = False
    for fc, t60, snr, why in dirac:
        if t60 is None:
            snrs = f"{snr:.0f} dB" if snr is not None else "no fit"
            reason = "below the noise" if why == "noise" else "decay not straight"
            print(f"  {fc:7.1f} Hz   -- {reason} ({snrs})")
        else:
            any_dirac = True
            band = band_means.get(fc)
            delta_s = f"   bursts {band:.2f} s" if band else ""
            print(f"  {fc:7.1f} Hz   T60 = {t60:.2f} s{delta_s}")
    if not any_dirac:
        print("  (nothing resolved -- expected: one sample of impulse is quiet)")

    allv = [v for _, v in good]
    t60_mean = float(np.mean(allv))
    print(f"\n  broadband mean T60 = {t60_mean:.2f} s "
          f"(min {min(allv):.2f}, max {max(allv):.2f})")

    if args.volume:
        fs_lo = schroeder_frequency(min(allv), args.volume)
        fs_hi = schroeder_frequency(max(allv), args.volume)
        fs_mid = schroeder_frequency(t60_mean, args.volume)
        print(f"\nSchroeder frequency for V = {args.volume:g} m^3")
        print(f"  {fs_mid:.0f} Hz from the mean T60 "
              f"(range {fs_lo:.0f}-{fs_hi:.0f} Hz across bursts)")
        print("  Below it the measurement describes this position; "
              "above it, the loudspeaker.")

    if args.dat:
        with open(args.dat, "w") as fh:
            fh.write("# frequency_Hz  T60_s\n")
            fh.write("# from %s\n" % args.recording)
            for f, v in good:
                fh.write(f"{f:.3f}  {v:.4f}\n")
        print(f"\nwrote {args.dat}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
