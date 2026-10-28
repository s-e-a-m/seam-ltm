# ALOGOS study folder

This folder holds the measurements and generators behind ALOGOS.

## Files

- `inphirev-table.cpp` writes INPHIREV's delay table as the fixture `tests/ref/alogos_inphirev_table.h`; it builds only at 1b4ac7a and earlier, while `plugins/inphirev` is in the tree.
- `cpu-spike.cpp` measures the cost of ALOGOS's heaviest setting on bare sections (Task 1, before the engine existed).
- `cpu.cpp` measures the cost of the engine itself at the default and the heaviest setting (the README's figures).
- `arena.cpp` prints the README's table of the scales (grid points, N_max, arena per rate).
- `gen-ref.sh` renders the Faust spec (`sgs.aldel`, `sgs.algrid`, `sgs.alogos`) into `tests/ref/alogos_ref.h`; it needs `faust` and runs by hand when the spec changes.
- `refdump.cpp` is the driver that `gen-ref.sh` compiles against each generated DSP; its header comment lists the modes and the array layouts.
- `dsp/aldel.dsp` exposes the delay and the grid through entries; `dsp/alogos.dsp` is the whole reverb, with its constants substituted by `gen-ref.sh`.

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

## CPU of the engine

    c++ -std=c++17 -O2 -I ../../../plugins/alogos/source -I ../../../plugins/_common -I ../../../tests cpu.cpp -o cpu && ./cpu

`cpu.cpp` is INPHIREV's harness ported onto `alogos::Engine`: the whole engine as the plugin runs it, 20 s of the stunedrev burst in 256-sample blocks.
Machine: Intel Core i7-8850H @ 2.60 GHz, macOS, one thread (2026-10-28, two runs, the first shown).

| setting | sections per sample | arena | cost |
|---|---|---|---|
| default, 48 kHz | 68 | 99.2 MiB | 1.3 % of one core |
| default, 96 kHz | 68 | 198.5 MiB | 2.8 % |
| default, 96 kHz, after 10 min of silence | 68 | 198.5 MiB | 2.7 % |
| heaviest: sqrt 2, D 4, 1 m, N 119, 96 kHz | 476 | 2928.3 MiB | 21.8 % |

The second run gave 1.3, 2.7, 2.5 and 21.5 %.
The engine costs less than the spike at the heaviest setting (21.8 % against 29.2 %); the two loops differ (the spike's sections live in vectors, the engine's in fixed arrays), and the difference was not investigated further.

## Arena table

    c++ -std=c++17 -O2 -I ../../../plugins/alogos/source -I ../../../plugins/_common arena.cpp -o arena && ./arena

`arena.cpp` prints the table of the scales in the plugin README: grid points, N_max at 3.33 m, and the largest arena of each scale and density over every STONE id and grid point at N_max, at 48, 96 and 192 kHz, with the engine's own functions.
