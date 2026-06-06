# X2UHJ design harness

Design-time tools (NOT part of the plugin build). Per CLAUDE.md these are
one-off sketch/measurement tools; they never emit production DSP code.

## Setup
    python3 -m venv .venv
    .venv/bin/pip install -r requirements.txt

## Run
    .venv/bin/python design_quadrature.py      # derive + emit coeffs + figures
    .venv/bin/python compare_empirical.py      # 2023 empirical vs analytic
