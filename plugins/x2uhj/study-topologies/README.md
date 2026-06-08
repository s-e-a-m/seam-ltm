# Quadrature Topology Study

This study compares quadrature all-pass topologies on a shared harness across phase accuracy, computational cost, sample-rate behaviour, and group delay.
It recommends a topology for a future standalone quadrature plugin.

## Scripts

| Script | Responsibility |
|---|---|
| `tools/topology.py` | Defines shared constants (`STD_RATES`, `F_LO`, `F_HI`, `TARGET`), helpers (`band_freqs`, `phase_error_deg`), and the uniform interface every topology implements. |
| `tools/topo_rbj.py` | Implements the RBJ second-order biquad cascade topology with per-sample-rate minimax design. |
| `tools/topo_polyphase.py` | Implements the Niemitalo first-order polyphase topology in fixed and per-sample-rate modes. |
| `tools/topo_halfband.py` | Implements the elliptic half-band derived Hilbert topology. |
| `tools/topo_historical.py` | Implements the Schroeder 1958 differential all-pass topology as an illustrative historical measurement. |
| `tools/compare.py` | Runs the comparison harness across all topologies and writes `../results.json`. |
| `tools/plots.py` | Reads `../results.json` and renders four figures into `../doc/figures/`. |

## How to run

```bash
cd plugins/x2uhj/study-topologies/tools
../../tools/.venv/bin/python compare.py     # writes ../results.json
../../tools/.venv/bin/python plots.py       # writes ../doc/figures/*.png
../../tools/.venv/bin/python -m pytest -v   # runs the test suite
cd ../doc && latexmk -pdf topology-study.tex   # builds the study PDF
```

## How it fits

`results.json` is the single source of truth from which the figures and the document tables derive.
Shared math comes from `../../tools/`.
The bibliography reuses `../../doc/math/refs.bib`.

## Scope

The study informs a future standalone quadrature plugin.
It changes nothing in the shipped x2uhj plugin or its coefficients.
