// cpu.cpp -- the cost of the delRM engine: percent of a core at 96 and
// 192 kHz, 256-sample blocks, the test signal then 60 s of silence (the
// silent tail is where subnormals would grow).
// c++ -std=c++17 -O3 -I ../../../../plugins/delrm/source -I ../../../../plugins/_common -I ../../../../tests cpu.cpp -o cpu
#include "delrm_dsp.h"
#include "delrm_signal.h"
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    for (double fs : {96000.0, 192000.0}) {
        delrm::Engine e;
        if (!e.prepare(fs)) { printf("allocation failed at %g\n", fs); continue; }
        e.setOutput(1.0); e.setPower(true); e.reset();
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
        DelrmSignal sig(fs);
        auto time = [&](double seconds, bool silent) {
            const long blocks = (long)(seconds * fs / B);
            const auto t0 = std::chrono::steady_clock::now();
            for (long b = 0; b < blocks; ++b) {
                if (silent) std::fill(ib.begin(), ib.end(), 0.0); else sig.fill(in, B);
                e.process(in, out, B);
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / seconds;
        };
        const double sound = time(20.0, false), silence = time(60.0, true);
        printf("%6.0f Hz: %.2f %% of a core on sound, %.2f %% on silence, lines %.2f MiB\n",
               fs, 100.0 * sound, 100.0 * silence, 4.0 * e.lineLength() * sizeof(double) / 1048576.0);
    }
}
