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
    std::vector<double> s((size_t)d.numSections, 0.0);
    for (int k = 0; k < n; ++k) {
        double x = (k == 0) ? 1.0 : 0.0;
        for (int i = 0; i < d.numSections; ++i) {
            const double y = d.b0[i] * x + s[(size_t)i];
            s[(size_t)i] = d.b1[i] * x - d.a1[i] * y;
            x = y;
        }
        printf("%.17g\n", x);
    }
    return 0;
}
