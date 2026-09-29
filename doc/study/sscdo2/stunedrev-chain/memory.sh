#!/bin/bash
# memory.sh -- memory of the generated C++ (Faust, -double): the sum of the
# arrays the DSP class declares, for Davide's stunedrev.dsp and sdt.stunedrev.
set -euo pipefail
NEW_LIBS=${NEW_LIBS:-/Users/giuseppe/Documents/github/grame/faustlibraries}
HERE=$(cd "$(dirname "$0")" && pwd)
SEAM_LIBS=${SEAM_LIBS:-$(cd "$HERE/../../../../../faust-libraries/src" && pwd)}
B=$HERE/build; mkdir -p "$B"
DAVIDE=${DAVIDE:-/Users/giuseppe/Documents/gitlab/dt/studio_sul_corpo_dombra_numero_2/src/FAUST/targets/stunedrev/stunedrev.dsp}
printf 'import("stdfaust.lib");\nimport("seam.lib");\nprocess = sdt.stunedrev(83, 47, 7, 71);\n' > "$B/seam_stunedrev.dsp"
size() { # file [faust options]
  local f=$1; shift
  faust -double "$@" -lang cpp -I "$NEW_LIBS" -I "$SEAM_LIBS" -I "$(dirname "$f")" "$f" 2>/dev/null |
    grep -E '^[[:space:]]+(double|float|int) [A-Za-z0-9_]+\[[0-9]+\];' |
    sed -E 's/.*(double|float|int) [^[]+\[([0-9]+)\];/\1 \2/' |
    awk '{ s += $2 * ($1 == "double" ? 8 : 4) } END { printf "%.2f GiB (%.0f MiB)\n", s / 2^30, s / 2^20 }'
}
echo
echo "| DSP | memory of the generated C++, double |"
echo "|---|---|"
if [ -f "$DAVIDE" ]; then echo "| Davide's stunedrev.dsp | $(size "$DAVIDE") |"; else echo "| Davide's stunedrev.dsp | not found (set DAVIDE=path) |"; fi
echo "| sdt.stunedrev | $(size "$B/seam_stunedrev.dsp") |"
echo "| sdt.stunedrev, -dlt 4096 | $(size "$B/seam_stunedrev.dsp" -dlt 4096) |"
python3 -c "
import math
def isp(n):
    if n<2: return False
    if n%2==0: return n==2
    return all(n%d for d in range(3,math.isqrt(n)+1,2))
def np_(n):
    c=n+2 if n&1 else n+1
    while not isp(c): c+=2
    return c
K=[math.sqrt(2),(1+5**.5)/2,math.e,math.pi]
b=sum(np_(int(math.floor(100*(i+1)*k*96+0.5)))*8 for k in K for i in range(42))
print(f'| C++ sized exactly at 96 kHz (longest delay of each section) | {b/2**30:.2f} GiB ({b/2**20:.0f} MiB) |')"
