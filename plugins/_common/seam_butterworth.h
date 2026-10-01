//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_butterworth.h — order-N Butterworth, SVF sections
//
// FAUST REFERENCE (filters.lib, faustlibraries 0965ea2, J.O. Smith, #262):
//
//   lowpass0_highpass1(s,N,fc) = lphpr(s,N,N,fc) with {
//     lphpr(s,O,N,fc) = lphpr(s,(O-2),N,fc) : section(s) with {
//       S   = O/2;                                   // section 1 .. N/2
//       a1s = -2*cos(-PI + PI/(2N) + (S-1)*PI/N);    // (even N)
//       section(0) = svf.lp(fc, 1/a1s);
//       section(1) = svf.hp(fc, 1/a1s);
//     };
//   };
//
// and svf (filters.lib), with k = 1/Q = a1s and g = tan(PI*fc/SR):
//
//   v1 = (ic1 + g*(v0 - ic2)) / (1 + g*(g + k));   v2 = ic2 + g*v1;
//   ic1' = 2*v1 - ic1;   ic2' = 2*v2 - ic2;
//   LP = v2;   HP = v0 - k*v1 - v2.
//
// Each section is the bilinear transform of a Butterworth biquad, prewarped
// at fc, in trapezoidal state-variable form: the integrator states keep
// low-fc/SR sections accurate where a direct-form biquad loses them. The
// damping k of each section depends only on N, so it is computed once; a
// frequency change recomputes g and one reciprocal per section — which is
// what makes a per-sample glissando cheap. Sections run in Faust's order,
// S = 1 first. Even N only: the first-order section of odd N is not needed
// by the suite yet.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <array>
#include <cmath>

namespace Seam {

enum class ButterworthType { Lowpass, Highpass };

template <int N>
class ButterworthSVF {
    static_assert(N >= 2 && N % 2 == 0, "ButterworthSVF: even orders only");
public:
    static constexpr int kSections = N / 2;

    ButterworthSVF() {
        constexpr double pi = 3.14159265358979323846;
        for (int s = 1; s <= kSections; ++s)
            k_[s - 1] = -2.0 * std::cos(-pi + pi / (2.0 * N) + (s - 1) * pi / N);
        setFrequency(1000.0, 48000.0);
    }

    void setType(ButterworthType t) { hp_ = (t == ButterworthType::Highpass); }

    void setFrequency(double fc, double fs) {
        constexpr double pi = 3.14159265358979323846;
        g_ = std::tan(pi * fc / fs);
        for (int s = 0; s < kSections; ++s)
            den_[s] = 1.0 / (1.0 + g_ * (g_ + k_[s]));
    }

    void reset() { ic1_.fill(0.0); ic2_.fill(0.0); }

    double tick(double x) {
        for (int s = 0; s < kSections; ++s) {
            const double v0 = x;
            const double v1 = (ic1_[s] + g_ * (v0 - ic2_[s])) * den_[s];
            const double v2 = ic2_[s] + g_ * v1;
            ic1_[s] = 2.0 * v1 - ic1_[s];
            ic2_[s] = 2.0 * v2 - ic2_[s];
            x = hp_ ? (v0 - k_[s] * v1 - v2) : v2;
        }
        return x;
    }

private:
    std::array<double, kSections> k_{}, den_{}, ic1_{}, ic2_{};
    double g_ = 0.0;
    bool hp_ = false;
};

} // namespace Seam
