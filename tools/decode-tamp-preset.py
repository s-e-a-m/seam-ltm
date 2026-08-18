#!/usr/bin/env python3
"""Decode a t.amp DSP Quadro 500 .preset file (software "QUADRO 500 DSP V2").

Written so that a STONE calibration documents itself: the amplifier's own
preset is the record of what was done, and transcribing it by hand is how the
numbers went missing from the 2026-08-18 session log.

FORMAT, reverse-engineered 2026-08-18 from
`2026-08-13-STONED-BRIDGE-FLAT.preset` plus three values read off the
software's UI (HPF 38.5 Hz, EQ1 90.5 Hz LSF +9 dB, EQ2 119.4 Hz HSF -18 dB).

    0x00  00 00                     unknown
    0x02  hpSlope lpSlope           filter slope selectors
    0x04  hpFreq lpFreq             crossover frequency indices, per channel
    0x06  hpFreq lpFreq             (repeated: the second channel)
    0x18  16 records of 6 bytes     channel A
    0x78  16 records of 6 bytes     channel B (identical in bridge mode)

    record: type, decade, index, q, gain, bypass
      type   1 = PEQ, 2 = LSF (low shelf), 3 = HSF (high shelf)
      freq   19.8 * 10^(decade + index/100)   Hz, within 0.3% of the UI
      gain   gain/2 - 18                      dB, so 36 -> 0.0 dB exactly
      q      raw; the scale is NOT yet solved. One point is known -- EQ4 in
             the sample file has raw 16 and the UI reads Q 1.0 -- and 35 is
             the value every untouched slot carries. One point cannot fix a
             two-parameter scale, so this is reported raw rather than
             converted: a calibration record with an invented Q in it is worse
             than one with a byte in it.
      bypass 0 = active, 1 = bypassed

Unverified: the q scale, and the meaning of records 9-16 (a second bank; only
one of them was set in the sample file).
"""
import sys

FREQ_ANCHOR = 19.8      # Hz; fitted on three UI readings, max error 0.28%
TYPES = {1: "PEQ", 2: "LSF", 3: "HSF"}


def freq(decade, index):
    return FREQ_ANCHOR * 10.0 ** (decade + index / 100.0)


def gain_db(raw):
    return raw / 2.0 - 18.0


def records(data, base):
    for i in range(16):
        r = data[base + i * 6: base + i * 6 + 6]
        yield i + 1, {"type": r[0], "decade": r[1], "index": r[2],
                      "q": r[3], "gain": r[4], "bypass": r[5]}


def main(path):
    data = open(path, "rb").read()
    a = data[0x18:0x78]
    b = data[0x78:0xD8]
    print("file      %s (%d bytes)" % (path, len(data)))
    print("channels  %s\n" % ("identical (bridge)" if a == b else "DIFFERENT"))

    print("crossover")
    print("  high pass  %6.1f Hz   slope byte %d" % (freq(0, data[0x04]), data[0x02]))
    print("  low pass   %6.1f Hz   slope byte %d   (check the UI for bypass)\n"
          % (freq(0, data[0x05]), data[0x03]))

    print("  slot  type    freq        gain    q(raw)  state")
    for n, r in records(data, 0x18):
        state = "bypass" if r["bypass"] else "ACTIVE"
        print("  %4d  %-4s  %8.1f Hz  %+6.1f dB  %6d  %s"
              % (n, TYPES.get(r["type"], "?%d" % r["type"]),
                 freq(r["decade"], r["index"]), gain_db(r["gain"]),
                 r["q"], state))
    active = sum(1 for _, r in records(data, 0x18) if not r["bypass"])
    print("\n%d active of 16" % active)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         "/Users/giuseppe/Desktop/2026-08-13-STONED-BRIDGE-FLAT.preset")
