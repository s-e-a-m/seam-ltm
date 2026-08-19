// Architecture file for the Faust/C++ A/B. Not a plug-in architecture:
// it feeds one impulse and prints the response, so two implementations can be
// compared sample by sample.
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
