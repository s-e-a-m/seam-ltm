// arena.cpp -- the table of the scales in the ALOGOS README: for each scale
// and density, the grid points per STONE id, N_max at the default distance
// (3.33 m, snapped on the grid of id 1), and the largest arena of that scale
// and density at 48, 96 and 192 kHz: the maximum, over every STONE id and
// grid point, of the doubles the engine allocates at N = N_max (each section
// delay + 1), in MiB. The same functions as the engine (alogos_dsp.h), so
// the figures are the footer's.
// From this directory:
// c++ -std=c++17 -O2 -I ../../../plugins/alogos/source -I ../../../plugins/_common arena.cpp -o arena && ./arena
#include "alogos_dsp.h"
#include <cstdio>
#include <memory>

using namespace alogos;

int main() {
    const char* names[] = { "phi", "sqrt 2", "e", "pi" };
    const double rates[] = { 48000.0, 96000.0, 192000.0 };
    std::unique_ptr<Seam::PrimeSieve> sieves[3];
    for (int r = 0; r < 3; ++r) sieves[r].reset(new Seam::PrimeSieve(sieveBound(rates[r])));
    std::printf("| scale | D | grid points | N_max at 3.33 m | heaviest | 48 kHz | 96 kHz | 192 kHz |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (int s = 0; s < 4; ++s)
        for (int dn = 0; dn < 3; ++dn) {
            const Scale sc = (Scale)s;
            const int D = density(dn), J = gridMax(sc, D);
            const double d333 = gridMetres(sc, D, 1, snapGrid(sc, D, 1, kDefaultMetres));
            double mib[3] = { 0, 0, 0 };
            double atM = 0; int atN = 0;
            for (int id = 1; id <= kMaxStoneId; ++id)
                for (int j = 0; j <= J; ++j) {
                    const double m = gridMetres(sc, D, id, j);
                    const int n = nMax(sc, D, m);
                    for (int r = 0; r < 3; ++r) {
                        double need = 0;
                        for (int k = 0; k < kFaces; ++k)
                            for (int i = 0; i < n; ++i)
                                need += (double)sectionDelay(sc, D, m, i, k, rates[r], *sieves[r]) + 1.0;
                        const double b = need * sizeof(double) / 1048576.0;
                        if (b > mib[r]) { mib[r] = b; if (r == 1) { atM = m; atN = n; } }
                    }
                }
            std::printf("| %s | %d | %d | %d (%.3f m) | %.3f m, N %d | %.0f MiB | %.0f MiB | %.0f MiB |\n",
                        names[s], D, J + 1, nMax(sc, D, d333), d333, atM, atN, mib[0], mib[1], mib[2]);
        }
}
