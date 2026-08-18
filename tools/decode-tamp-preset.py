#!/usr/bin/env python3
"""Decode a t.amp DSP Quadro 500 .preset file (software "QUADRO 500 DSP V2").

Written so that a STONE calibration documents itself: the amplifier's preset is
the record of what was done, and transcribing nine filters by hand at the end
of a long session is how the numbers went missing from the 2026-08-18 log.

FORMAT, reverse-engineered 2026-08-18 from `2026-08-13-STONED-BRIDGE-FLAT.preset`
and checked against a screenshot of the software showing CH1:

    0x00  00 00
    0x02  hpType lpType        crossover slope selectors (2 = BW24)
    0x04  hpFreq lpFreq        crossover indices, bridge pair A
    0x06  hpFreq lpFreq        bridge pair B
    0x08  16 zero bytes
    0x18  8 records x 6 bytes  CH1 EQ1..EQ8
    0x48  8 records x 6 bytes  CH2
    0x78  8 records x 6 bytes  CH3
    0xa8  8 records x 6 bytes  CH4
    0xd8  tail: per-channel gain, delay, limiter, routing (only partly read)

    record: type, decade, index, q, gain, bypass
      type   1 = Peak, 2 = LSF (low shelf), 3 = HSF (high shelf)
      freq   1000 * 2^((100*decade + index - 170)/30)   Hz
      q      2^((q - 16)/12)          -- so 16 = 1.0, 28 = 2.0, 35 = 3.0
      gain   gain/2 - 18              -- so 36 = 0.0 dB exactly
      bypass 0 = active, 1 = bypassed

Both scales are 12-tone-equal-temperament grids: the frequency advances by
1/30 of an octave per step and the Q by a semitone ratio. Checked against nine
values read off the UI, the frequency model is within 0.07% on seven of them
and 0.6% on the two the software displays with only three significant digits;
the Q model is exact on all three known values.

NOT resolved: the low-pass index (bypassed in the sample, and 0x3b does not
decode to the 20.16 kHz the UI showed), and most of the tail. Those are printed
raw rather than guessed at -- a decoder that invents numbers for a calibration
record is worse than one that admits a gap.
"""
import sys

TYPES = {1: "Peak", 2: "LSF", 3: "HSF"}
EQ_BASE = 0x18
CHANNELS = 4
SLOTS = 8


def freq(decade, index):
    """1/30 octave steps, anchored so that index 170 is exactly 1 kHz."""
    return 1000.0 * 2.0 ** ((100 * decade + index - 170) / 30.0)


def q_factor(raw):
    return 2.0 ** ((raw - 16) / 12.0)


def gain_db(raw):
    return raw / 2.0 - 18.0


def channel(data, ch):
    base = EQ_BASE + ch * SLOTS * 6
    for i in range(SLOTS):
        r = data[base + i * 6: base + i * 6 + 6]
        yield i + 1, r


def main(path):
    data = open(path, "rb").read()
    print("file       %s (%d bytes)" % (path, len(data)))
    print("crossover  high pass %.1f Hz (slope byte %d) · low pass index %d raw"
          % (freq(0, data[0x04]), data[0x02], data[0x05]))

    banks = []
    for ch in range(CHANNELS):
        rows = []
        for slot, r in channel(data, ch):
            rows.append((slot, r))
        banks.append(rows)

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
