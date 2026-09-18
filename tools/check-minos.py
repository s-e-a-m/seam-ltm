#!/usr/bin/env python3
"""SEAM-LTM minimum-macOS lint.

Reads the LC_BUILD_VERSION load command out of every built Mach-O handed to
it and fails when one of them declares a minimum macOS newer than the version
the suite ships for. Every slice of a universal binary is checked separately.

WHY THIS EXISTS: when nothing sets CMAKE_OSX_DEPLOYMENT_TARGET, CMake
initialises it from the macOS of the machine running the build. The binaries
then refuse to load on any older system, with no build warning and no error
anywhere — the Finder simply draws a prohibitory badge on the bundle, and only
someone else's Mac ever sees it. v0.2.0 and v0.3.0 both shipped pinned to
macOS 15.7 that way.

Standard library only: this must run from a bare python3, with no pip step,
on any machine that can build the suite.

Usage:  check-minos.py <path> [<path> ...]
        A path may be a .vst3 bundle, a directory of them, or a plain Mach-O.

Exit code 0 when every slice checked is within the ceiling, 1 otherwise.
"""
import os
import re
import subprocess
import sys

# The macOS this suite is built to run on, as (major, minor).
#
# This constant is DELIBERATELY not read from CMAKE_OSX_DEPLOYMENT_TARGET.
# A test whose threshold comes from the same variable that sets the value
# under test can never fail: raise the target to 26.0 and the comparison
# raises with it. The number here is the shipping policy, written down
# independently, so that a build which quietly targets something newer is
# exactly what turns this test red.
MAX_MINOS = (11, 0)

BUILD_VERSION_RE = re.compile(
    r"\(architecture (?P<arch>[^)]+)\):|^\s*minos (?P<minos>[0-9.]+)\s*$",
    re.MULTILINE,
)


def parse_version(text):
    """'11.0' or '15.7.9' -> (11, 0) / (15, 7). Minor missing means 0."""
    parts = text.split(".")
    major = int(parts[0])
    minor = int(parts[1]) if len(parts) > 1 else 0
    return (major, minor)


def slices(path):
    """Yield (arch, (major, minor)) for each slice declaring LC_BUILD_VERSION.

    vtool prints one '(architecture X):' header per slice of a fat binary and
    none at all for a thin one, so the arch carried forward is 'thin' until a
    header says otherwise.
    """
    out = subprocess.run(
        ["vtool", "-show-build", path],
        capture_output=True, text=True,
    )
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip() or "vtool failed on %s" % path)

    arch = "thin"
    for m in BUILD_VERSION_RE.finditer(out.stdout):
        if m.group("arch"):
            arch = m.group("arch")
        else:
            yield arch, parse_version(m.group("minos"))


def machos(path):
    """Expand a path into the Mach-O files to inspect."""
    if os.path.isdir(path):
        if path.endswith(".vst3"):
            name = os.path.basename(path)[: -len(".vst3")]
            exe = os.path.join(path, "Contents", "MacOS", name)
            if os.path.isfile(exe):
                yield exe
            return
        for entry in sorted(os.listdir(path)):
            if entry.endswith(".vst3") or entry.endswith(".dylib"):
                for f in machos(os.path.join(path, entry)):
                    yield f
        return
    if os.path.isfile(path):
        yield path


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 1

    if sys.platform != "darwin":
        print("check-minos: not macOS, nothing to check")
        return 0

    targets = []
    for arg in argv[1:]:
        targets.extend(machos(arg))

    if not targets:
        # A lint that finds nothing to lint passes for the wrong reason.
        print("ERROR no Mach-O found under: %s" % ", ".join(argv[1:]))
        return 1

    ceiling = "%d.%d" % MAX_MINOS
    failures = 0
    checked = 0
    for path in targets:
        name = os.path.basename(path)
        try:
            found = list(slices(path))
        except RuntimeError as exc:
            print("%s: ERROR %s" % (name, exc))
            failures += 1
            continue

        if not found:
            print("%s: ERROR no LC_BUILD_VERSION" % name)
            failures += 1
            continue

        for arch, minos in found:
            checked += 1
            shown = "%d.%d" % minos
            if minos > MAX_MINOS:
                print("%s (%s): ERROR minos %s exceeds the shipping floor %s"
                      % (name, arch, shown, ceiling))
                failures += 1
            else:
                print("%s (%s): ok minos %s" % (name, arch, shown))

    print("--- %d slice(s) checked against macOS %s, %d failure(s)"
          % (checked, ceiling, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
