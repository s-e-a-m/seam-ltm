// Architecture file for the Faust/C++ A/B. Not a plug-in architecture:
// it feeds one impulse and prints the response, so two implementations can be
// compared sample by sample.
//
// FAUSTFLOAT must be `double`, defined before faust/dsp/dsp.h is included:
// that header's own `#ifndef FAUSTFLOAT #define FAUSTFLOAT float #endif`
// (faust/dsp/dsp.h) would otherwise satisfy the generated class's identical
// guard first, leaving the I/O buffers and the final
// `output0[i0] = static_cast<FAUSTFLOAT>(...)` in single precision even
// though `-double` was passed to `faust` -- `-double` only widens the
// INTERNAL arithmetic. Defined here, in the architecture file, rather than
// as a `-D` on the compiler invocation, so the fix travels with the tool
// and cannot be dropped by a future edit to faust-pink-ab.sh.
#define FAUSTFLOAT double
#include <cstdio>
#include <cstdlib>
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

<<includeIntrinsic>>
<<includeclass>>

int main(int argc, char* argv[]) {
    const int sr = (argc > 1) ? atoi(argv[1]) : 48000;
    const int n  = (argc > 2) ? atoi(argv[2]) : 4096;
    mydsp DSP;
    DSP.init(sr);
    FAUSTFLOAT* in  = new FAUSTFLOAT[n]();
    FAUSTFLOAT* out = new FAUSTFLOAT[n]();
    in[0] = (FAUSTFLOAT)1.0;
    FAUSTFLOAT* ins[]  = { in };
    FAUSTFLOAT* outs[] = { out };
    DSP.compute(n, ins, outs);
    for (int i = 0; i < n; ++i) printf("%.17g\n", (double)out[i]);
    return 0;
}
