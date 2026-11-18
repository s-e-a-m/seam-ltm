// Prints an impulse response of BASTOOGE for the Faust/C++ A/B
// (faust-bastooge-ab.sh). Two modes:
//   bastooge-ir-dump <w|d> <fc> <fs> <n>
//     one correction chain. W: Bell(mk2gBell(fc)) -> Allpass(fc, sqrt(0.5)).
//     D: two Highpass(fc, sqrt(0.5)).
//   bastooge-ir-dump e <fc> <fs> <n> <test> <bypass> <in> <face>
//     the whole Seam::bastooge::Engine (selector, filters, bypass, matrix):
//     a unit impulse on ACN input <in> (0..3), output face <face> (0..3 =
//     LFU, RFD, RBU, LBD), with test 0..4 and bypass 0/1.
#include "bastooge_dsp.h"
#include "seam_st.h"
#include "seam_svf.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int engine(int argc, char* argv[]) {
    if (argc < 9) {
        fprintf(stderr, "usage: %s e <fc> <fs> <n> <test> <bypass> <in> <face>\n", argv[0]);
        return 2;
    }
    const double fc = atof(argv[2]);
    const double fs = atof(argv[3]);
    const int n     = atoi(argv[4]);
    const int test  = atoi(argv[5]);
    const bool byp  = atoi(argv[6]) != 0;
    const int inCh  = atoi(argv[7]);
    const int face  = atoi(argv[8]);
    if (test < 0 || test > 4 || inCh < 0 || inCh > 3 || face < 0 || face > 3) {
        fprintf(stderr, "test 0..4, in 0..3, face 0..3\n");
        return 2;
    }
    Seam::bastooge::Engine e;
    e.setCut(fc);           // before prepare, which snaps the smoother to it
    e.setTest((Seam::bastooge::Test)test);
    e.setBypass(byp);
    e.prepare(fs);
    std::vector<double> buf[8];
    for (auto& b : buf) b.assign(n, 0.0);
    buf[inCh][0] = 1.0;
    const double* in[4] = {buf[0].data(), buf[1].data(), buf[2].data(), buf[3].data()};
    double* out[4] = {buf[4].data(), buf[5].data(), buf[6].data(), buf[7].data()};
    e.process(in, out, n);
    for (int k = 0; k < n; ++k) printf("%.17g\n", out[face][k]);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::strcmp(argv[1], "e") == 0) return engine(argc, argv);
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
