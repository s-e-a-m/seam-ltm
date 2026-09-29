#!/bin/bash
# build.sh -- compile one LMO band-filter candidate to an offline binary.
#
# usage: ./build.sh PROC LIBSET PREC [OPT]
#   PROC    process name in dsp/cands.dsp (hplp, davide, bp24, bp3, bp2, noise0)
#   LIBSET  old | new     which faustlibraries tree to import stdfaust.lib from
#   PREC    single | double
#   OPT     C++ optimisation level (default O2)
# output: build/bin/PROC_LIBSET_PREC_OPT
#
# ./build.sh all   builds every binary the analysis needs.
set -euo pipefail

# ---- paths (edit these on another machine) ---------------------------------
# NEW: a clone of https://github.com/grame-cncm/faustlibraries, used as is
#      (the results in results.md were measured at 9c42142, after 0965ea2 / #262
#      and 4b251bf / #261).
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
# OLD: the faustlibraries shipped with Faust 2.72.14, which built Davide's Pd
#      externals. The compiler pins it as its `libraries` submodule
#      (git -C <faust> ls-tree 2.72.14 libraries -> d28c51f6, 2024-03-20).
#      Extracted once from the NEW clone's history into build/libs-old.
OLD_COMMIT=${OLD_COMMIT:-d28c51f6}
FAUST=${FAUST:-faust}
CXX=${CXX:-c++}
# ----------------------------------------------------------------------------

HERE=$(cd "$(dirname "$0")" && pwd)
BIN=$HERE/build/bin
mkdir -p "$BIN"
OLD_LIBS=${OLD_LIBS:-$HERE/build/libs-old}
if [ ! -f "$OLD_LIBS/stdfaust.lib" ]; then
  mkdir -p "$OLD_LIBS"
  git -C "$NEW_LIBS" archive "$OLD_COMMIT" | tar -x -C "$OLD_LIBS"
  echo "extracted faustlibraries $OLD_COMMIT into $OLD_LIBS"
fi

build_one() {
  local proc=$1 lib=$2 prec=$3 opt=${4:-O2}
  local libdir
  case $lib in old) libdir=$OLD_LIBS ;; new) libdir=$NEW_LIBS ;; *) echo "bad libset $lib" >&2; exit 2 ;; esac
  local out=$BIN/${proc}_${lib}_${prec}_${opt}
  # make-style: skip when the binary is newer than every source (FORCE=1 rebuilds)
  if [ -z "${FORCE:-}" ] && [ -x "$out" ] && [ "$out" -nt "$HERE/dsp/cands.dsp" ] && [ "$out" -nt "$HERE/dsp/ctl.lib" ] \
     && [ "$out" -nt "$HERE/harness/arch.cpp" ] && [ "$out" -nt "$HERE/build.sh" ]; then echo "up to date $out"; return; fi
  # -I libdir first: stdfaust.lib is resolved from the chosen tree, never from faust's bundled copy.
  "$FAUST" -t 0 -$prec -pn "$proc" -I "$libdir" -I "$HERE/dsp" -a "$HERE/harness/arch.cpp" "$HERE/dsp/cands.dsp" -o "$out.cpp"
  "$CXX" -$opt -std=c++17 -I"$("$FAUST" --includedir)" "$out.cpp" -o "$out" 2>&1 | { grep -v -i warning || true; }
  echo "built $out"
}

if [ "${1:-}" = all ]; then
  build_one noise0 new double
  build_one hplp old double      # A
  build_one davide old double    # A at Davide's true level
  build_one hplp new double      # B
  build_one bp24 new double      # C (slow: about 4 minutes of Faust compilation)
  build_one bp3 new double       # C3
  build_one bp2 new double       # C2
  exit 0
fi

build_one "$@"
