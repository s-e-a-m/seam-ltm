// cpu.cpp -- the cost of the alogos engine: percent of a core, 256-sample
// blocks, the burst then silence, at the default setting (phi, D 1, 3.33 m,
// N 17, g 1/phi) at 48 and 96 kHz, at 96 kHz also after 10 minutes of silence
// (the subnormal check), and at the heaviest setting (sqrt 2, D 4, 1 m,
// N 128 -> N_max 119, g 1/sqrt 2: 476 sections per sample) at 96 kHz.
// The port of INPHIREV's cpu.cpp onto alogos::Engine: the whole engine as the
// plugin runs it (the 250 ms settle, POWER and the fades included), not the
// bare sections of cpu-spike.cpp.
// From this directory:
// c++ -std=c++17 -O2 -I ../../../plugins/alogos/source -I ../../../plugins/_common -I ../../../tests cpu.cpp -o cpu && ./cpu
#include "alogos_dsp.h"
#include "stunedrev_burst.h"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace alogos;

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
    struct Case { const char* name; double fs, metres; int n, scale, dn, gain; bool soak; } cases[] = {
        { "default ", 48000.0, 3.33, 17,  0, 0, 0, false },
        { "default ", 96000.0, 3.33, 17,  0, 0, 0, true  },
        { "heaviest", 96000.0, 1.0,  128, 1, 2, 1, false },
    };
    for (const Case& c : cases) {
        Engine e;
        e.setSettings(1, c.metres, c.n, c.scale, c.dn, c.gain);
        if (!e.prepare(c.fs)) { printf("allocation failed at %g\n", c.fs); continue; }
        e.setPower(true); e.reset();
        Burst burst(c.fs);
        const double seconds = 20.0;
        const double cpu = timed(e, burst, c.fs, seconds);
        printf("%s %6.0f Hz, %.4f m, N %d (%d per sample): %.1f %% of a core (%.2f s of CPU for %.0f s), "
               "arena %.1f MiB\n", c.name, c.fs, e.metres(), e.sections(), 4 * e.sections(),
               100.0 * cpu / seconds, cpu, seconds, e.arenaBytes() / 1048576.0);
        if (!c.soak) continue;
        timed(e, burst, c.fs, 600.0);
        const double cpu2 = timed(e, burst, c.fs, seconds);
        printf("%s %6.0f Hz after 10 min of silence: %.1f %% of a core\n", c.name, c.fs, 100.0 * cpu2 / seconds);
    }
}
