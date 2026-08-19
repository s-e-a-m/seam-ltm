// Prints the impulse response of PinkDesign, for the Faust/C++ A/B.
#include "multipink_pink.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char* argv[]) {
    const double fs = (argc > 1) ? atof(argv[1]) : 48000.0;
    const int    n  = (argc > 2) ? atoi(argv[2]) : 4096;
    Seam::multipink::PinkDesign d;
    d.design(fs);
    // The recurrence is not written out here: it is the shipped one,
    // Seam::multipink::pinkFilterBlock, run over a single stream in double so
    // that the A/B compares the design and not a transcription of it.
    std::vector<double> ir((size_t)n, 0.0);
    if (n > 0) ir[0] = 1.0;
    std::vector<double> s((size_t)d.numSections, 0.0);
    Seam::multipink::pinkFilterBlock<double, double>(d, ir.data(), 1, n, s.data(), 1);
    for (int k = 0; k < n; ++k) printf("%.17g\n", ir[(size_t)k]);
    return 0;
}
