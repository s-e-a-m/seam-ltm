#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "bamodulex_engine.h"

#include <cstdint>
#include <vector>

using Seam::pd::bamodulexBlock;

namespace {

constexpr int kN = 1024;

struct Frames {
    std::vector<float> ch[4];
    Frames ()
    {
        uint32_t s = 777u;
        for (auto& c : ch) {
            c.resize (kN);
            for (auto& x : c) {
                s = s * 1664525u + 1013904223u;
                x = (float)((double)(s >> 8) / (double)(1u << 24) * 2.0 - 1.0);
            }
        }
    }
};

// Result with four separate input and four separate output buffers.
Frames separate (const Frames& in)
{
    Frames out;
    const float* i[4] = { in.ch[0].data (), in.ch[1].data (), in.ch[2].data (), in.ch[3].data () };
    float* o[4] = { out.ch[0].data (), out.ch[1].data (), out.ch[2].data (), out.ch[3].data () };
    bamodulexBlock (i, o, kN);
    return out;
}

} // namespace

TEST_CASE("block equals decode") {
    const Frames in;
    const Frames out = separate (in);
    for (int n = 0; n < kN; ++n) {
        float ref[4];
        Seam::bamodulex::decode<float> (in.ch[0][n], in.ch[1][n], in.ch[2][n], in.ch[3][n], ref);
        for (int k = 0; k < 4; ++k)
            REQUIRE(out.ch[k][n] == ref[k]);
    }
}

TEST_CASE("in-place: every out[k] aliasing in[k]") {
    const Frames in;
    const Frames ref = separate (in);
    Frames buf = in;
    float* p[4] = { buf.ch[0].data (), buf.ch[1].data (), buf.ch[2].data (), buf.ch[3].data () };
    const float* ip[4] = { p[0], p[1], p[2], p[3] };
    bamodulexBlock (ip, p, kN);
    for (int k = 0; k < 4; ++k)
        for (int n = 0; n < kN; ++n)
            REQUIRE(buf.ch[k][n] == ref.ch[k][n]);
}

TEST_CASE("in-place: out[0] aliasing in[3]") {
    const Frames in;
    const Frames ref = separate (in);
    Frames ins = in, outs;
    outs.ch[0] = in.ch[3];                  // the buffer Pd shares
    const float* ip[4] = { ins.ch[0].data (), ins.ch[1].data (), ins.ch[2].data (), outs.ch[0].data () };
    float* op[4] = { outs.ch[0].data (), outs.ch[1].data (), outs.ch[2].data (), outs.ch[3].data () };
    bamodulexBlock (ip, op, kN);
    for (int k = 0; k < 4; ++k)
        for (int n = 0; n < kN; ++n)
            REQUIRE(outs.ch[k][n] == ref.ch[k][n]);
}
