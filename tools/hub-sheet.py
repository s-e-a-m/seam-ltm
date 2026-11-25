#!/usr/bin/env python3
"""Parser for the SEAM hub session sheet (grammar: plugins/_common/hub/seam_hub_sheet.h).

parse(path) -> list of dicts(time, who, verb, object, rows).
  --check FILE   exit 0 if every non-header line is an event or a continuation
                 following an event; else print the first bad line number, exit 1.
  --count FILE   print the number of events.
Standard library only.
"""
import re
import sys

VERBS = ("arrives", "leaves", "state", "says", "shows", "applies", "ignores", "sheet")
EVENT = re.compile(r"^(\d\d:\d\d:\d\d\.\d\d\d)  (\S+) *  (%s)(?:  (.*))?$" % "|".join(VERBS))
ROW = re.compile(r"^  \| (.*)$")


def parse_lines(lines):
    """Yield (lineno, kind, payload); kind is header, event, row or bad."""
    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\n")
        if line.startswith("# "):
            yield n, "header", line
            continue
        m = EVENT.match(line)
        if m:
            yield n, "event", dict(time=m.group(1), who=m.group(2), verb=m.group(3),
                                   object=(m.group(4) or "").strip(), rows=[])
            continue
        m = ROW.match(line)
        if m:
            yield n, "row", m.group(1)
            continue
        yield n, "bad", line


def parse(path):
    events = []
    with open(path, encoding="utf-8") as f:
        for n, kind, p in parse_lines(f):
            if kind == "event":
                events.append(p)
            elif kind == "row":
                if not events:
                    raise ValueError("line %d: row before any event" % n)
                events[-1]["rows"].append(p)
            elif kind == "bad":
                raise ValueError("line %d: not an event or a row: %r" % (n, p))
    return events


def check(path):
    seen_event = False
    with open(path, encoding="utf-8") as f:
        for n, kind, _ in parse_lines(f):
            if kind == "event":
                seen_event = True
            elif kind == "row" and not seen_event:
                return n
            elif kind == "bad":
                return n
    return 0


def main(argv):
    if len(argv) == 3 and argv[1] == "--check":
        bad = check(argv[2])
        if bad:
            print("bad line %d" % bad)
            return 1
        return 0
    if len(argv) == 3 and argv[1] == "--count":
        print(len(parse(argv[2])))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
