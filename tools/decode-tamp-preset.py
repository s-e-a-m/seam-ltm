#!/usr/bin/env python3
"""Decode a t.amp DSP Quadro 500 .preset file (software "QUADRO 500 DSP V2").

Written so that a STONE calibration documents itself: the amplifier's preset is
the record of what was done, and transcribing nine filters by hand at the end
of a long session is how the numbers went missing from the 2026-08-18 log.

FORMAT, reverse-engineered 2026-08-18 from `2026-08-13-STONED-BRIDGE-FLAT.preset`
and checked against a screenshot of the software showing CH1:

    0x00  4 bytes              unknown
    0x04  4 bytes              CHANNEL GAIN, one per channel:
                                 dB = (byte - 59) / 2, so 59 is 0.0 dB
    0x08  16 zero bytes
    0x18  8 records x 6 bytes  CH1 EQ1..EQ8
    0x48  8 records x 6 bytes  CH2
    0x78  8 records x 6 bytes  CH3
    0xa8  8 records x 6 bytes  CH4
    0xdc  4 records x 8 bytes  crossover, one per channel:
                                 +0 lpType  +1 lpDecade  +2 lpIndex  +3 pad
                                 +4 hpType  +5 hpDecade  +6 hpIndex  +7 pad
    0xfc  4 records x 5 bytes  COMPRESSOR, one per channel:
                                 threshold (byte - 100)/2 dB
                                 ratio     index into RATIOS below
                                 attack    (byte + 1) * 10 ms
                                 release   (byte + 1) * 20 ms
                                 bypass    1 = bypassed, as in the EQ records
    0x110 workmode             0 = four channels, 3 = two bridged pairs
    0x113 4 bytes              input routing per channel

    EQ record: type, decade, index, q, gain, bypass
      type   1 = Peak, 2 = LSF (low shelf), 3 = HSF (high shelf)
      freq   1000 * 2^((100*decade + index - 170)/30)   Hz
      q      2^((q - 16)/12)          -- so 16 = 1.0, 28 = 2.0, 35 = 3.0
      gain   gain/2 - 18              -- so 36 = 0.0 dB exactly
      bypass 0 = active, 1 = bypassed

    crossover type: 0 = Bypass, 7 = BE12, 11 = BW24, 21 = BW48. It is a menu
    index, not a slope, so unknown values are printed raw. The frequency uses
    the same grid as the EQ, and keeps its stored value while bypassed.

Both scales are 12-tone-equal-temperament grids: the frequency advances by
1/30 of an octave per step and the Q by a semitone ratio. Checked against nine
values read off the UI, the frequency model is within 0.07% on seven of them
and 0.6% on the two the software displays with only three significant digits;
the Q model is exact on all three known values.

The crossover section was settled by a controlled test (GS, 2026-08-18): five
presets differing in one parameter each. lp-1k and lp-10.08k differ in a single
byte, the decade, and decode to exactly 1000.0 and 10079.4 Hz; lp-bypass
differs from lp-10.08k only in the type byte and keeps its frequency; a
hand-set 100 Hz BE12 high pass decodes to 101.5 Hz, the nearest point on the
grid. The first reading of this file put the crossovers in the header, where
two bytes happened to match: the test is what caught it.

The channel gain was found the same way, and by the same mistake: byte 88 in
the 0xfc block looked like a gain and was not. The real field is in the header
and reads (byte - 59)/2 dB, exact on nine independently declared values across
four presets -- including two channels one step apart, -12.0 and -12.5 dB.

The compressor turned out to live in that same 0xfc block, and its four
parameters fall out of two readings apiece: threshold -6 and -12.5 dB against
bytes 88 and 75, attack 100 and 10 ms against 9 and 0, release 300 and 500 ms
against 14 and 24. The bypass flag keeps the EQ convention, 1 = bypassed.

NOT decoded: the noise gate, and deliberately -- it is unused in this rig, so
no preset exists that would show what its bytes do. Everything else the
software exposes is read.
"""
import argparse
import cmath
import math
import sys

TYPES = {1: "Peak", 2: "LSF", 3: "HSF"}
XTYPES = {0: "Bypass", 7: "BE12", 11: "BW24", 21: "BW48"}
WORKMODE = {0: "four channels", 3: "two bridged pairs"}
XOVER_BASE = 0xDC
GAIN_BASE = 0x04      # header, one byte per channel
GAIN_ZERO = 59        # the byte that reads 0.0 dB
# The compressor ratio is a menu index, and the menu is an irregular list --
# read off the software by the operator, not derived. Index 5 is 2.00 and index
# 22 is LIMIT, which is exactly what the two known presets carry.
RATIOS = ["1", "1.17", "1.28", "1.47", "1.69", "2.00", "2.17", "2.23", "2.71",
          "3.03", "3.29", "3.48", "3.89", "4.59", "4.98", "6.06", "6.96",
          "7.78", "8.22", "8.68", "9.71", "10.08", "LIMIT"]

COMP_BASE = 0xFC
COMP_ZERO = 100       # the threshold byte that reads 0.0 dB
EQ_BASE = 0x18
CHANNELS = 4
SLOTS = 8


def freq(decade, index):
    """1/30 octave steps, anchored so that index 170 is exactly 1 kHz."""
    return 1000.0 * 2.0 ** ((100 * decade + index - 170) / 30.0)


def q_factor(raw):
    return 2.0 ** ((raw - 16) / 12.0)


def gain_db(raw):
    """EQ filter gain."""
    return raw / 2.0 - 18.0


def channel_gain_db(raw):
    """Output gain of a channel, a different scale from the EQ gains."""
    return (raw - GAIN_ZERO) / 2.0


def compressor(data, ch):
    r = data[COMP_BASE + ch * 5: COMP_BASE + ch * 5 + 5]
    return {"threshold": (r[0] - COMP_ZERO) / 2.0,
            "ratio": RATIOS[r[1]] if r[1] < len(RATIOS) else "index %d" % r[1],
            "attack": (r[2] + 1) * 10,
            "release": (r[3] + 1) * 20,
            "bypass": bool(r[4])}


def channel(data, ch):
    base = EQ_BASE + ch * SLOTS * 6
    for i in range(SLOTS):
        r = data[base + i * 6: base + i * 6 + 6]
        yield i + 1, r


# ---------------------------------------------------------------------------
# Response model -- what the filter bank does, drawn from what the preset says.
#
# The amplifier's own topology is not published, so this assumes the RBJ Audio
# EQ Cookbook forms (peaking, low shelf and high shelf with S=1) evaluated as
# digital biquads at kDspFs, and cascaded Butterworth sections for the BW
# crossover slopes. That assumption is DECLARED, not established: the
# per-filter curves are exact, since they follow only from decoded parameters,
# but the SUM is provisional until it has been checked against the curve the
# QUADRO 500 software draws for the same preset.
#
# Bessel (BE) slopes are not modelled: their alignment is a different filter
# family, and guessing it would put an invented curve next to measured ones.
# ---------------------------------------------------------------------------

kDspFs = 48000.0          # assumed sample rate of the amplifier's DSP


def _biquad_db(b, a, f, fs):
    """20*log10|H(e^jw)| of one biquad section at frequency f."""
    w = 2.0 * math.pi * f / fs
    z1 = cmath.exp(-1j * w)
    num = b[0] + b[1] * z1 + b[2] * z1 * z1
    den = a[0] + a[1] * z1 + a[2] * z1 * z1
    if den == 0:
        return float("-inf")
    m = abs(num / den)
    return 20.0 * math.log10(m) if m > 0 else float("-inf")


def _rbj(kind, f0, q, gain, fs):
    """RBJ Audio EQ Cookbook coefficients, normalised to a0 = 1."""
    w0 = 2.0 * math.pi * f0 / fs
    cw, sw = math.cos(w0), math.sin(w0)
    if kind == "peak":
        A = 10.0 ** (gain / 40.0)
        al = sw / (2.0 * q)
        b = [1 + al * A, -2 * cw, 1 - al * A]
        a = [1 + al / A, -2 * cw, 1 - al / A]
    elif kind in ("lowshelf", "highshelf"):
        A = 10.0 ** (gain / 40.0)
        al = sw / 2.0 * math.sqrt(2.0)          # S = 1, the standard slope
        tsa = 2.0 * math.sqrt(A) * al
        if kind == "lowshelf":
            b = [A * ((A + 1) - (A - 1) * cw + tsa),
                 2 * A * ((A - 1) - (A + 1) * cw),
                 A * ((A + 1) - (A - 1) * cw - tsa)]
            a = [(A + 1) + (A - 1) * cw + tsa,
                 -2 * ((A - 1) + (A + 1) * cw),
                 (A + 1) + (A - 1) * cw - tsa]
        else:
            b = [A * ((A + 1) + (A - 1) * cw + tsa),
                 -2 * A * ((A - 1) + (A + 1) * cw),
                 A * ((A + 1) + (A - 1) * cw - tsa)]
            a = [(A + 1) - (A - 1) * cw + tsa,
                 2 * ((A - 1) - (A + 1) * cw),
                 (A + 1) - (A - 1) * cw - tsa]
    elif kind == "highpass":
        al = sw / (2.0 * q)
        b = [(1 + cw) / 2.0, -(1 + cw), (1 + cw) / 2.0]
        a = [1 + al, -2 * cw, 1 - al]
    else:
        raise ValueError(kind)
    return [x / a[0] for x in b], [x / a[0] for x in a]


def butterworth_qs(order):
    """Section Qs of a Butterworth cascade of even order."""
    return [1.0 / (2.0 * math.cos(math.pi * (2 * k + 1) / (2 * order)))
            for k in range(order // 2)]


BW_ORDER = {"BW12": 2, "BW24": 4, "BW48": 8}


def eq_response_db(rec, f):
    """dB of one EQ slot at f. rec is the raw 6-byte record."""
    kind = {1: "peak", 2: "lowshelf", 3: "highshelf"}.get(rec[0])
    if kind is None:
        return 0.0
    q = q_factor(rec[3]) if kind == "peak" else 1.0
    b, a = _rbj(kind, freq(rec[1], rec[2]), q, gain_db(rec[4]), kDspFs)
    return _biquad_db(b, a, f, kDspFs)


def highpass_response_db(xtype, f0, f):
    """dB of the channel high pass, or None when the slope is not modelled."""
    order = BW_ORDER.get(xtype)
    if order is None:
        return None
    return sum(_biquad_db(*_rbj("highpass", f0, q, 0.0, kDspFs), f, kDspFs)
               for q in butterworth_qs(order))


def log_grid(f_lo=20.0, f_hi=20000.0, points=400):
    step = (math.log10(f_hi) - math.log10(f_lo)) / (points - 1)
    return [10.0 ** (math.log10(f_lo) + i * step) for i in range(points)]


def write_dat(path, data, ch):
    """Emit a pgfplots table: one column per active filter, plus the sum."""
    base = EQ_BASE + (ch - 1) * SLOTS * 6
    slots = [(i + 1, data[base + i * 6: base + i * 6 + 6]) for i in range(SLOTS)]
    slots = [(s, r) for s, r in slots if r[5] == 0]

    x = data[XOVER_BASE + (ch - 1) * 8: XOVER_BASE + (ch - 1) * 8 + 8]
    hp_type = XTYPES.get(x[4], "?%d" % x[4])
    hp_f0 = freq(x[5], x[6])
    hp_on = hp_type in BW_ORDER

    cols = (["hp"] if hp_on else []) + ["eq%d" % s for s, _ in slots] + ["total"]
    with open(path, "w") as fh:
        fh.write("# SEAM -- t.amp filter bank, decoded from the preset\n")
        fh.write("# channel %d, RBJ biquads at %g Hz (assumed DSP rate)\n"
                 % (ch, kDspFs))
        if hp_on:
            fh.write("# high pass %s at %.1f Hz, order %d Butterworth cascade\n"
                     % (hp_type, hp_f0, BW_ORDER[hp_type]))
        else:
            fh.write("# high pass %s NOT modelled, and NOT in the total\n" % hp_type)
        for s, r in slots:
            q = "%.2f" % q_factor(r[3]) if r[0] == 1 else "-"
            fh.write("# eq%d %-4s %8.1f Hz  Q %-5s %+.1f dB\n"
                     % (s, TYPES.get(r[0], "?"), freq(r[1], r[2]), q, gain_db(r[4])))
        fh.write("# per-filter columns are exact; the total assumes the RBJ "
                 "forms and is provisional\n")
        fh.write("freq " + " ".join(cols) + "\n")
        for f in log_grid():
            vals = []
            if hp_on:
                vals.append(highpass_response_db(hp_type, hp_f0, f))
            vals += [eq_response_db(r, f) for _, r in slots]
            vals.append(sum(vals))
            fh.write("%.4f " % f + " ".join("%.4f" % v for v in vals) + "\n")
    return cols, hp_type, hp_on


def main(path, dat=None, dat_channel=1):
    data = open(path, "rb").read()
    print("file       %s (%d bytes)" % (path, len(data)))
    print("workmode   %s" % WORKMODE.get(data[0x110], "raw %d" % data[0x110]))

    banks = []
    for ch in range(CHANNELS):
        rows = []
        for slot, r in channel(data, ch):
            rows.append((slot, r))
        banks.append(rows)

    for ch in range(CHANNELS):
        x = data[XOVER_BASE + ch * 8: XOVER_BASE + ch * 8 + 8]
        print("CH%d  high pass %-6s %9.1f Hz   low pass %-6s %9.1f Hz   gain %+.1f dB"
              % (ch + 1, XTYPES.get(x[4], "?%d" % x[4]), freq(x[5], x[6]),
                 XTYPES.get(x[0], "?%d" % x[0]), freq(x[1], x[2]),
                 channel_gain_db(data[GAIN_BASE + ch])))
        c = compressor(data, ch)
        print("     compressor %-8s %+6.1f dB  ratio %-8s attack %3d ms  release %3d ms"
              % ("bypassed" if c["bypass"] else "ACTIVE", c["threshold"],
                 c["ratio"], c["attack"], c["release"]))

    for ch, rows in enumerate(banks, 1):
        active = [(s, r) for s, r in rows if r[5] == 0]
        same = next((c + 1 for c in range(ch - 1)
                     if banks[c] == rows), None)
        note = "  (identical to CH%d)" % same if same else ""
        print("\nCH%d — %d active of %d%s" % (ch, len(active), SLOTS, note))
        if not active:
            print("  (all bypassed)")
            continue
        print("  slot  type   freq          Q     gain")
        for slot, r in active:
            # The software puts the type in its QFACT column for shelving
            # filters, so their q byte is an untouched default, not a setting.
            q = "%5.2f" % q_factor(r[3]) if r[0] == 1 else "    -"
            print("  EQ%d   %-4s  %8.1f Hz  %s  %+6.1f dB"
                  % (slot, TYPES.get(r[0], "?%d" % r[0]),
                     freq(r[1], r[2]), q, gain_db(r[4])))

    if dat:
        cols, hp_type, hp_on = write_dat(dat, data, dat_channel)
        print("\nwrote %s -- CH%d, columns: %s" % (dat, dat_channel, " ".join(cols)))
        if not hp_on:
            print("  the %s high pass is not modelled, and is absent from the total"
                  % hp_type)
        print("  per-filter curves are exact; the total assumes RBJ forms and is\n"
              "  provisional until checked against the software's own curve")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("preset")
    ap.add_argument("--dat", help="write a pgfplots table of the filter curves here")
    ap.add_argument("--channel", type=int, default=1,
                    help="channel whose bank the --dat table describes (default 1)")
    ap.add_argument("--dsp-fs", type=float, default=kDspFs,
                    help="assumed sample rate of the amplifier DSP (default 48000)")
    args = ap.parse_args()
    kDspFs = args.dsp_fs
    main(args.preset, args.dat, args.channel)
