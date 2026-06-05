// SEAM-LTM · X2UHJ — SDK-free DSP core (header-only, unit-testable).
#pragma once
#include <cmath>

namespace Seam { namespace x2uhj {

// Canonical RBJ 2nd-order all-pass. Same math as tools/rbj.py.
struct AllpassSection {
    double a1 = 0, a2 = 0;          // b0=a2, b1=a1, b2=1 (all-pass symmetry)
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set(double f, double Q, double fs) {
        const double w = 2.0 * M_PI * f / fs;
        const double alpha = std::sin(w) / (2.0 * Q);
        const double n = 1.0 + alpha;
        a1 = -2.0 * std::cos(w) / n;
        a2 = (1.0 - alpha) / n;
    }
    void reset() { x1 = x2 = y1 = y2 = 0; }
    double process(double x) {
        const double y = a2 * x + a1 * x1 + 1.0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

}} // namespace
