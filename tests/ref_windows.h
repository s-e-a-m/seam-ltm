//──────────────────────────────────────────────────────────────────────────
// ref_windows.h — compare a C++ render with a windowed Faust reference
//
// The references hold, per output, the 512 samples from every fs/8 and the
// energy of every fs/8 (doc/study/sscdo2/delrm-plugin/refdump.cpp). A test
// feeds the same signal in the same 256-sample blocks, calls see() for every
// output sample, and reads the largest error relative to the reference's
// peak, and the largest relative error of the energies.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace refwin {

struct Compare {
    Compare(int outputs, int windows, long period)
        : no(outputs), nw(windows), period(period), energy((size_t)outputs * windows, 0.0) {}

    // Output c, global sample g, value y; ref = &kRef[0][0][0], eref = &kRef_energy[0][0].
    void see(int c, long g, double y, const double* ref) {
        const int w = (int)(g / period); const long off = g % period;
        if (w >= nw) return;
        energy[(size_t)c * nw + w] += y * y;
        if (off < 512) {
            const double r = ref[((size_t)c * nw + w) * 512 + off];
            maxErr = std::max(maxErr, std::fabs(y - r));
            peak = std::max(peak, std::fabs(r));
        }
    }
    double relErr() const { return peak > 0.0 ? maxErr / peak : maxErr; }
    double energyRelErr(const double* eref) const {
        double worst = 0.0;
        for (size_t i = 0; i < energy.size(); ++i) {
            const double e = eref[i];
            worst = std::max(worst, e > 0.0 ? std::fabs(energy[i] - e) / e : std::fabs(energy[i]));
        }
        return worst;
    }

    int no, nw; long period;
    std::vector<double> energy;
    double maxErr = 0.0, peak = 0.0;
};

} // namespace refwin
