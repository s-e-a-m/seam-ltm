//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · Pd · multipink~ engine — four calibrated pink streams
//
// DSP: plugins/multipink/source/multipink_pink.h (shared with the VST3)
//
// No parameters: the output is what a 4-channel SEAM MULTIPINK instance on
// pool slots 1-4 gives at Reference -23 dBFS, Trim 0 — sample for sample.
// Total RMS -23 dBFS per channel at 48 kHz; at other rates the third-octave
// band level holds still and the total RMS moves (doc/calibration.md).
//
// Only the four streams are run, not the plug-in's 64: each stream depends
// on its own seed and state alone, so the other 60 change nothing here.
//─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "multipink_pink.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Seam { namespace pd {

class MultipinkEngine
{
public:
    static constexpr int      kChannels    = 4;
    static constexpr double   kReferenceDb = -23.0;
    static constexpr uint32_t kMasterSeed  = 12345;   // MULTIPINKProcessor::kFaustSeed

    // Not real-time: designs the filter, allocates, and restarts the streams
    // from their seeds. Pd calls it from the dsp method.
    void prepare (double fs, int maxBlock)
    {
        design_.design (fs);
        gain_ = (float) std::pow (10.0, (kReferenceDb + design_.calibrationOffsetDb ()) / 20.0);
        for (int k = 0; k < kChannels; ++k)
            lcg_[k] = multipink::poolSeed (kMasterSeed, k);
        std::memset (state_, 0, sizeof (state_));
        maxBlock_ = maxBlock;
        scratch_.assign ((size_t) kChannels * (size_t) maxBlock, 0.0f);
    }

    // n <= maxBlock. The outputs may share memory with nothing in particular:
    // the streams are built in scratch and only then written out.
    void process (float* const out[kChannels], int n)
    {
        for (int k = 0; k < kChannels; ++k)
            lcg_[k] = multipink::whiteNoiseRow<float> (lcg_[k], scratch_.data () + (size_t) k * n, n);
        multipink::pinkFilterBlock<float> (design_, scratch_.data (), kChannels, n,
                                          &state_[0][0], kChannels);
        for (int k = 0; k < kChannels; ++k) {
            const float* src = scratch_.data () + (size_t) k * n;
            for (int i = 0; i < n; ++i)
                out[k][i] = src[i] * gain_;
        }
    }

    int maxBlock () const { return maxBlock_; }

private:
    multipink::PinkDesign design_;
    float    gain_ = 0.0f;
    uint32_t lcg_[kChannels] = {};
    float    state_[multipink::PinkDesign::kMaxSections][kChannels] = {};   // section-major, as in the VST3
    std::vector<float> scratch_;
    int      maxBlock_ = 0;
};

}} // namespace Seam::pd
