// cpu.cpp -- the cost of the inphirev engine at 18 sections (72 per sample, the
// maximum): percent of a core at 48 and 96 kHz, 256-sample blocks, the burst
// then silence; at 96 kHz also after 10 minutes of silence (the subnormal
// check), and with the longest grid point (reads scattered over the largest arena).
// c++ -std=c++17 -O2 -I ../../../plugins/inphirev/source -I ../../../plugins/_common -I ../../../tests cpu.cpp -o cpu
#include "inphirev_dsp.h"
#include "stunedrev_burst.h"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace inphirev;

static double timed(Engine& e, Burst& burst, double fs, double seconds) {
    const int B = 256;
    std::vector<double> ib(4 * B), ob(4 * B);
    double* in[4]; double* out[4];
    for (int c = 0; c < 4; ++c) { in[c] = ib.data() + c * B; out[c] = ob.data() + c * B; }
    const long blocks = (long)(seconds * fs / B);
    const auto t0 = std::chrono::steady_clock::now();
    for (long b = 0; b < blocks; ++b) { burst.fill(in, B); e.process((const double* const*)in, out, B); }
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    struct Case { double fs; int j; } cases[] = { {48000.0, 10}, {96000.0, 10}, {96000.0, kGridMax} };
    for (const Case& c : cases) {
        Engine e;
        e.setSettings(1, c.j, kMaxSections);
        if (!e.prepare(c.fs)) { printf("allocation failed at %g\n", c.fs); continue; }
        e.setPower(true); e.reset();
        Burst burst(c.fs);
        const double seconds = 20.0;
        const double cpu = timed(e, burst, c.fs, seconds);
        printf("%6.0f Hz, j %2d (%.3f m), N %d: %.1f %% of a core (%.2f s of CPU for %.0f s), arena %.0f MiB\n",
               c.fs, c.j, e.metres(), e.sections(), 100.0 * cpu / seconds, cpu, seconds, e.arenaBytes() / 1048576.0);
        if (c.fs != 96000.0 || c.j != 10) continue;
        timed(e, burst, c.fs, 600.0);
        const double cpu2 = timed(e, burst, c.fs, seconds);
        printf("%6.0f Hz after 10 min of silence: %.1f %% of a core\n", c.fs, 100.0 * cpu2 / seconds);
    }
}
