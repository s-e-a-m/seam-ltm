// SEAM-LTM · seam_meter — shared metering facility (Phase A).
//
// Header-only, SDK-free. Three-layer metering system (measure / transport /
// render); this header is the MEASURE + normalization layer, usable from a
// plugin's SDK-free <plugin>_dsp.h or from its processor. Transport is the
// multipink read-only-output-parameter idiom; render is the uidesc convention.
// See docs/superpowers/specs/2026-07-11-seam-ltm-metering-system-design.md.
#pragma once
#include <cmath>

namespace seam { namespace meter {

// Linear amplitude -> dBFS, floored (silence maps to floorDb, never -inf).
inline double lin2db(double x, double floorDb = -60.0) {
    if (x <= 0.0) return floorDb;
    const double db = 20.0 * std::log10(x);
    return db < floorDb ? floorDb : db;
}

// dB -> normalized [0,1] over [floorDb, 0], clamped. A degenerate floor
// (floorDb >= 0) has an empty range; return a defined 0/1 instead of NaN.
inline double db2norm(double db, double floorDb = -60.0) {
    const double span = 0.0 - floorDb;
    if (span <= 0.0) return db >= 0.0 ? 1.0 : 0.0;
    const double n = (db - floorDb) / span;
    return n < 0.0 ? 0.0 : (n > 1.0 ? 1.0 : n);
}

// Linear amplitude -> normalized [0,1] bar value.
inline double lin2norm(double x, double floorDb = -60.0) {
    return db2norm(lin2db(x, floorDb), floorDb);
}

// Normalized [0,1] -> dB (for labels).
inline double norm2db(double n, double floorDb = -60.0) {
    return floorDb + n * (0.0 - floorDb);
}

// One-pole level follower (RMS or peak) with a time-constant window.
class LevelFollower {
public:
    enum class Mode { Peak, Rms };
    void prepare(double fs, Mode mode = Mode::Rms, double windowMs = 300.0) {
        fs_   = (fs > 0.0) ? fs : 48000.0;
        mode_ = mode;
        const double tau = windowMs * 0.001;
        coef_ = (tau > 0.0) ? std::exp(-1.0 / (tau * fs_)) : 0.0;
        reset();
    }
    void reset() { state_ = 0.0; }
    double feed(double x) {
        const double v = (mode_ == Mode::Rms) ? x * x : std::fabs(x);
        state_ = v + coef_ * (state_ - v);      // one-pole toward v
        return value();
    }
    double value() const {
        return (mode_ == Mode::Rms) ? std::sqrt(state_) : state_;
    }
private:
    double fs_ = 48000.0, coef_ = 0.0, state_ = 0.0;
    Mode   mode_ = Mode::Rms;
};

}} // namespace seam::meter
