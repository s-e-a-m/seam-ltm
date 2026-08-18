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


def main(path):
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


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         "/Users/giuseppe/Desktop/2026-08-13-STONED-BRIDGE-FLAT.preset")
