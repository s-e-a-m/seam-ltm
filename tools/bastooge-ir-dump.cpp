// Prints the impulse response of the BASTOOGE W or dipole chain, for the
// Faust/C++ A/B (faust-bastooge-ab.sh).
//   bastooge-ir-dump <w|d> <fc> <fs> <n>
// W: Bell(mk2gBell(fc)) -> Allpass(fc, sqrt(0.5)).  D: two Highpass(fc, sqrt(0.5)).
#include "seam_st.h"
#include "seam_svf.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc < 5) { fprintf(stderr, "usage: %s <w|d> <fc> <fs> <n>\n", argv[0]); return 2; }
    const bool   w  = std::strcmp(argv[1], "w") == 0;
    const double fc = atof(argv[2]);
    const double fs = atof(argv[3]);
    const int    n  = atoi(argv[4]);
    using Seam::Svf;
    Svf a, b;
    const double q = std::sqrt(0.5);
    if (w) {
        const Seam::st::Bell bell = Seam::st::mk2gBell(fc);
        a.design(fs, Svf::Type::Bell, bell.f0, bell.q, bell.gainDb);
        b.design(fs, Svf::Type::Allpass, fc, q);
    } else {
        a.design(fs, Svf::Type::Highpass, fc, q);
        b.design(fs, Svf::Type::Highpass, fc, q);
    }
    a.reset(); b.reset();
    for (int k = 0; k < n; ++k)
        printf("%.17g\n", b.tick(a.tick(k == 0 ? 1.0 : 0.0)));
    return 0;
}
