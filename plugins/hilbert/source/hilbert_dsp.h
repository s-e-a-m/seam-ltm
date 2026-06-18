// SEAM-LTM · hilbert — SDK-free DSP core (header-only, unit-testable).
//
// A wideband 90° quadrature transformer: mono in → in-phase / quadrature out.
// Both outputs are all-pass-filtered; their phase difference holds −90° across
// the audio band. Neither output is the dry signal — the quadrature relation is
// a property of the *pair* (as in the x2uhj encoder's H_R/H_I networks).
//
// Two topologies, each designed live at the host sample rate by the shared
// seam_quadrature engine, so accuracy holds across rates:
//   RBJ        — 3 second-order all-pass biquads per branch (≈1.36° @48k)
//   Polyphase  — 4 first-order all-pass sections per path (≈1.67° @48k), 1/3 cost
//
// FAUST REFERENCE (seam.filters.lib): the quadrature/Hilbert pair (roadmap).
#pragma once
#include "seam_quadrature.h"

namespace Seam { namespace hilbert {

enum class Topology { RBJ = 0, Polyphase = 1 };

class HilbertTransformer {
public:
    // Design both topologies at `fs` and configure the active one.
    void prepare(double fs) {
        fs_ = fs;
        rbj_  = quadrature::designQuadrature(fs, 20.0, 20000.0, 3);
        poly_ = quadrature::designPolyphase(fs, 4);
        configure();
    }

    void setTopology(Topology t) {
        if (t == topo_) return;
        topo_ = t;
        configure(); // re-points the pair (and resets its state)
    }
    Topology topology() const { return topo_; }

    void reset() { pair_.reset(); }

    // x → (inPhase, quad); quad lags inPhase by 90° across the band.
    inline void process(double x, double& inPhase, double& quad) {
        pair_.process(x, inPhase, quad);
    }

    // Readout/UI accessors.
    const quadrature::QuadratureDesign& rbjDesign()  const { return rbj_; }
    const quadrature::PolyphaseDesign&  polyDesign() const { return poly_; }
    double sampleRate() const { return fs_; }

private:
    void configure() {
        if (topo_ == Topology::RBJ) pair_.configureRBJ(rbj_);
        else                        pair_.configurePolyphase(poly_);
    }

    double   fs_   = 0.0;
    Topology topo_ = Topology::RBJ;
    quadrature::QuadratureDesign rbj_;
    quadrature::PolyphaseDesign  poly_;
    quadrature::QuadraturePair   pair_;
};

}} // namespace Seam::hilbert
