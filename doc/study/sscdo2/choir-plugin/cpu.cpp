// cpu.cpp -- the cost of the choir engine: percent of a core at 96 and
// 48 kHz, 256-sample blocks, 20 s of the test signal then 60 s of silence
// (the silent tail is where subnormals would grow).
// c++ -std=c++17 -O3 -I ../../../../plugins/choir/source -I ../../../../plugins/_common cpu.cpp -o cpu
#include "choir_dsp.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    for (double fs : {96000.0, 48000.0}) {
        choir::Engine e; e.prepare(fs);
        e.setOutput(1.0); e.setPower(true); e.reset();
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        const double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
        long t = 0;
        auto time = [&](double seconds, bool silent) {
            const long blocks = (long)(seconds * fs / B);
            const auto t0 = std::chrono::steady_clock::now();
            for (long b = 0; b < blocks; ++b) {
                for (int c = 0; c < 4; ++c)
                    for (int i = 0; i < B; ++i) {
                        double x = 0.0;
                        if (!silent) for (int k = 1; k <= 16; ++k)
                            x += 0.05 * std::sin(2 * M_PI * e.config().f[(size_t)c] * k * (double)(t + i) / fs);
                        ib[(size_t)(c * B + i)] = x;
                    }
                t += B;
                e.process(in, out, B);
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / seconds;
        };
        const double sound = time(20.0, false), silence = time(60.0, true);
        printf("%6.0f Hz: %.2f %% of a core on sound (input synthesis included), %.2f %% on silence\n",
               fs, 100.0 * sound, 100.0 * silence);
    }
}
