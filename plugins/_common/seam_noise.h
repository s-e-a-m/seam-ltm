//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · seam_noise.h — the standard Faust multinoise, bit for bit
//
// FAUST REFERENCE (noises.lib, _noise_env(seed)):
//
//   multirandom(N) = randomize(N) ~ _
//   with {
//       randomize(1) = +(seed) : *(1103515245);
//       randomize(N) = randomize(1) <: randomize(N-1), _;
//   };
//   multinoise(N) = multirandom(N) : par(i, N, /(RANDMAX)) : par(i, N, float);
//
// ONE 32-bit linear congruential generator, stepped N times per sample
// inside one feedback loop: stream k is not separately seeded, it is step
// N-1-k of a single sequence (the recursion emits the deepest step first,
// and that same step is the one fed back). Within one call the streams are
// decorrelated (measured, doc/study/sscdo2/lmo-streams/); two calls with the
// same seed give the same noise. The seed is the additive constant of the
// LCG, 12345 in no.multinoise; another seed is another sequence.
//
// Arithmetic: Faust's int is 32-bit with wraparound, which unsigned
// arithmetic reproduces exactly; the result is reinterpreted as signed and
// scaled by 1/2147483647 in double (the constant Faust emits,
// 4.656612875245797e-10, is that double to the bit).
//
// FAUST REFERENCE (seam.noises.lib):
//
//   multinoiseblock(M, o, N) = no.multinoise(M) : si.block(o), si.bus(N),
//                                                 si.block(M-o-N);
//
// MultinoiseBlock: streams o .. o+N-1 of one multinoise(M). Machines that
// must be decorrelated read disjoint blocks of the SAME M: each value of
// the generator goes to one of them only. Another M or another seed reuses
// the same numbers (another M: at other instants; another seed: times a
// constant mod 2^32), see seam-ltm logs/2026-09-29-sscdo2-ricognizione.md,
// "The choir".
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstdint>
#include <vector>

namespace Seam {

class FaustMultinoise {
public:
    explicit FaustMultinoise(int n, int32_t seed = 12345)
        : n_(n), seed_(seed), steps_((size_t)n, 0) {}

    void reset() { state_ = 0; }
    int  size() const { return n_; }

    // Writes n_ values in [-1, 1] to out[0 .. n_-1].
    void tick(double* out) {
        uint32_t x = (uint32_t)state_;
        for (int s = 0; s < n_; ++s) {
            x = (x + (uint32_t)seed_) * 1103515245u;
            steps_[(size_t)s] = (int32_t)x;
        }
        state_ = steps_[(size_t)(n_ - 1)];
        for (int k = 0; k < n_; ++k)
            out[k] = kScale * (double)steps_[(size_t)(n_ - 1 - k)];
    }

private:
    static constexpr double kScale = 1.0 / 2147483647.0;   // 1/RANDMAX
    int n_;
    int32_t seed_;
    int32_t state_ = 0;
    std::vector<int32_t> steps_;
};

class MultinoiseBlock {
public:
    MultinoiseBlock(int m, int o, int n)
        : all_(m), o_(o), n_(n), buf_((size_t)m, 0.0) {}

    void reset() { all_.reset(); }
    int  size() const { return n_; }

    // Steps the whole generator, writes streams o_ .. o_+n_-1 to out[0 .. n_-1].
    void tick(double* out) {
        all_.tick(buf_.data());
        for (int k = 0; k < n_; ++k) out[k] = buf_[(size_t)(o_ + k)];
    }

private:
    FaustMultinoise all_;
    int o_, n_;
    std::vector<double> buf_;
};

} // namespace Seam
