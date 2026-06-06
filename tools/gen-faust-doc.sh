#!/usr/bin/env bash
#
# gen-faust-doc.sh — regenerate Faust block diagrams and mathematical
# documentation for SEAM-LTM plugins.
#
# Each plugin's doc/<name>.dsp points to the canonical seam Faust library
# (e.g. process = sam.x2uhj;). This script resolves those imports via
# FAUST_LIB_PATH and produces, in plugins/<name>/doc/:
#   <name>-svg/   block-diagram SVGs        (faust -svg)
#   <name>.pdf    mathematical documentation (faust2mathdoc)
#
# Requirements: faust, faust2mathdoc, svg2pdf, pdflatex (with breqn).
#   macOS:  brew install faust svg2pdf  +  a TeX distribution (MacTeX/BasicTeX)
#
# Usage:
#   tools/gen-faust-doc.sh              # all plugins
#   tools/gen-faust-doc.sh x2uhj sdmx  # named plugins
#
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

# Canonical seam Faust libraries (sibling checkout); override by exporting
# FAUST_LIB_PATH before running.
export FAUST_LIB_PATH="${FAUST_LIB_PATH:-$ROOT/../faust-libraries/src}"

if [ ! -f "$FAUST_LIB_PATH/seam.lib" ]; then
    echo "error: seam.lib not found in FAUST_LIB_PATH=$FAUST_LIB_PATH" >&2
    echo "       set FAUST_LIB_PATH to the faust-libraries/src directory." >&2
    exit 1
fi

gen() {
    local name=$1
    local docdir="$ROOT/plugins/$name/doc"
    if [ ! -f "$docdir/$name.dsp" ]; then
        echo "skip $name (no doc/$name.dsp)"
        return
    fi
    (
        cd "$docdir"
        rm -rf "$name-svg" "$name-mdoc"
        faust -I "$FAUST_LIB_PATH" -svg "$name.dsp" -o /dev/null
        faust2mathdoc "$name.dsp" >/dev/null
        cp "$name-mdoc/pdf/$name.pdf" "$name.pdf"
        rm -rf "$name-mdoc"
        echo "done $name: $(ls "$name-svg"/*.svg | wc -l | tr -d ' ') svg + $name.pdf"
    )
}

if [ $# -gt 0 ]; then
    for p in "$@"; do gen "$p"; done
else
    for d in "$ROOT"/plugins/*/; do
        [ -d "$d/doc" ] && gen "$(basename "$d")"
    done
fi
