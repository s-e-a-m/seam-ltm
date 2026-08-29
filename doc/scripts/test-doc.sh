#!/usr/bin/env bash
# I check usano confronti diretti e non grep -P: dentro uno script si usa
# /usr/bin/grep (BSD), che non ha -P.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/_data"; : > "$TMP/_data/navigation.yml"

EXPECTED_CHECKS=12
fail=0; checks=0
ok()  { checks=$((checks+1)); echo "  ok   $1"; }
bad() { checks=$((checks+1)); echo "  FAIL $1"; fail=1; }

n="$(python3 -c "
import sys; sys.path.insert(0,'$HERE')
import registry
print(sum(len(f['plugin']) for f in registry.load()))
")"
if [ "$n" -eq 16 ]; then ok "16 plugin nel registro"; else bad "16 plugin nel registro (trovati $n)"; fi

# il README deve essere gia rigenerato: se il registro e cambiato senza
# rigenerare, le due copie sono gia divergenti
cp "$ROOT/README.md" "$TMP/README.before"
python3 "$HERE/render-readme.py" >/dev/null
if diff -q "$TMP/README.before" "$ROOT/README.md" >/dev/null; then ok "README allineato al registro"; else bad "README non rigenerato dopo una modifica al registro"; fi

python3 "$HERE/publish.py" "$TMP" > "$TMP/out.txt" 2>&1 || { echo "publish.py errore:"; cat "$TMP/out.txt"; exit 1; }
P="$TMP/_ltm/index.md"

if [ -f "$P" ]; then ok "pagina generata"; else bad "pagina generata"; fi
if grep -q '^permalink: /seam-ltm/$' "$P"; then ok "permalink"; else bad "permalink"; fi
if grep -q '^generated_from: seam-ltm$' "$P"; then ok "provenienza"; else bad "provenienza"; fi
if [ "$(grep -c '^### ' "$P")" -eq 16 ]; then ok "16 schede"; else bad "16 schede"; fi
if grep -q '### MULTIPINK' "$P"; then ok "MULTIPINK presente"; else bad "MULTIPINK presente"; fi
if grep -q 'seam.ambisonics.lib' "$P"; then ok "rimando alla libreria Faust"; else bad "rimando alla libreria Faust"; fi
if [ "$(ls "$TMP/assets/seam-ltm/img" | wc -l | tr -d ' ')" -eq 16 ]; then ok "16 screenshot copiati"; else bad "16 screenshot copiati"; fi

# build e installazione restano nel README, non sul sito
if ! grep -qi 'cmake' "$P"; then ok "niente istruzioni di build sul sito"; else bad "niente istruzioni di build sul sito"; fi

if grep -q '# BEGIN ltm' "$TMP/_data/navigation.yml"; then ok "blocco nav"; else bad "blocco nav"; fi
python3 "$HERE/publish.py" "$TMP" >/dev/null 2>&1
b="$(grep -c '# BEGIN ltm' "$TMP/_data/navigation.yml")"
if [ "$b" -eq 1 ]; then ok "publish idempotente sul nav"; else bad "publish idempotente sul nav (blocchi: $b)"; fi

if [ "$checks" -ne "$EXPECTED_CHECKS" ]; then echo "  FAIL check eseguiti: $checks, attesi: $EXPECTED_CHECKS"; fail=1; fi
[ $fail -eq 0 ] && echo "TEST DOC OK ($checks check)" || { echo "TEST DOC FAIL"; exit 1; }
