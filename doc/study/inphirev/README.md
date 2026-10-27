# inphirev reference generator

`gen-ref.sh` renders the Faust spec (`sgs.inphirev` in `seam.silvi.lib`) into `tests/ref/inphirev_ref.h`.
The tests read the committed header and need no `faust` binary.
Run the script by hand when the spec changes.

`refdump.cpp` is the C++ harness around the Faust-generated class.
It prints the delay tables (`ipdel`), the constant `sma.rsos` (`tsos`) and the burst windows (`windows`).
The windows follow the layout of `stunedrev_ref.h`: 512 samples per second over 30 s for four channels, with a `_energy` array beside each.

`dsp/ipdel.dsp` outputs the delays of all nine ids at the `j` entry, because `ipphase` needs `id` as a compile-time constant.
`dsp/inphirev.dsp` fixes N in the source, and `gen-ref.sh` substitutes N = 1, 4 and 17.
`dsp/tsos.dsp` outputs `sma.rsos`.

The Faust class sizes its buffers for 192 kHz, so the N = 17 build allocates about 1 GB of doubles; `refdump` allocates it on the heap.
The input is the noise burst of `tests/stunedrev_burst.h`, shared with stunedrev.
