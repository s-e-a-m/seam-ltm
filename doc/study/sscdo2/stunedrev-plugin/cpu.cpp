// cpu.cpp -- the cost of the stunedrev engine: seconds of audio per second of
// CPU at 96 and 192 kHz, 256-sample blocks, the burst then silence; at 96 kHz
// also after 10 minutes of silence (the subnormal check).
// c++ -std=c++17 -O3 -I ../../../../plugins/stunedrev/source -I ../../../../plugins/_common -I ../../../../tests cpu.cpp -o cpu
#include "stunedrev_dsp.h"
#include "stunedrev_burst.h"
#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    for (double fs : {96000.0, 192000.0}) {
        stunedrev::Engine e;
        if (!e.prepare(fs)) { printf("allocation failed at %g\n", fs); continue; }
        e.setInput(1.0); e.setOutput(1.0); e.setPower(true); e.reset();
        const int B = 256; const double seconds = 20.0;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* in[4]; double* out[4];
        for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
        Burst burst(fs);
        const long blocks = (long)(seconds * fs / B);
        const auto t0 = std::chrono::steady_clock::now();
        for (long b = 0; b < blocks; ++b) { burst.fill(in, B); e.process(in, out, B); }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("%6.0f Hz: %.1f %% of a core (%.2f s of CPU for %.0f s), arena %.0f MiB\n",
               fs, 100.0 * cpu / seconds, cpu, seconds, e.arenaBytes() / 1048576.0);
        if (fs != 96000.0) continue;
        // The silent tail: 10 minutes, then 20 s measured again. Without the
        // engine's flush-to-zero, subnormals pile up and this grows for hours.
        const long tail = (long)(600.0 * fs / B);
        for (long b = 0; b < tail; ++b) { burst.fill(in, B); e.process(in, out, B); }
        const auto t1 = std::chrono::steady_clock::now();
        for (long b = 0; b < blocks; ++b) { burst.fill(in, B); e.process(in, out, B); }
        const double cpu2 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        printf("%6.0f Hz after 10 min of silence: %.1f %% of a core\n", fs, 100.0 * cpu2 / seconds);
    }
}
