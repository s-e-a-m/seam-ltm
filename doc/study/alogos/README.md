# ALOGOS study folder

This folder holds the measurements and generators behind ALOGOS.

## Files

- `inphirev-table.cpp` writes INPHIREV's delay table as the fixture `tests/ref/alogos_inphirev_table.h`.
- `cpu-spike.cpp` measures the cost of ALOGOS's heaviest setting.

## CPU spike

Build and run, from this directory:

    c++ -std=c++17 -O2 -I ../../../plugins/_common -I ../../../tests cpu-spike.cpp -o cpu-spike && ./cpu-spike

The spike runs 4 faces of Moorer all-pass sections with the Global Constraints delays, in `double`, under `ScopedNoDenormals`.
It feeds the stunedrev burst for 20 s of audio at 96 kHz in 256-sample blocks.

Machine: Intel Core i7-8850H @ 2.60 GHz, macOS, one thread.

| setting | sections per sample | longest delay | arena | cost |
|---|---|---|---|---|
| heaviest: sqrt 2, D 4, 1 m, N 119, g 1/sqrt 2 | 476 | 85.67 s | 2928.3 MiB | 29.2 % of one core |
| default: phi, D 1, 3.3302 m, N 17, g 1/phi | 68 | 30.72 s | 198.5 MiB | 3.0 % of one core |

The longest delay of the heaviest setting (face 3, section 118) is 85.674 s, within 90 s.
N = 120 would reach 93.428 s, above 90 s, so N_max = 119 holds.

## Gate

The heaviest setting costs 29.2 % of one core, below the 50 % threshold.
Outcome: **no ceiling**; there is no `kMaxSectionsPerSample`, and N is limited only by N_max.
