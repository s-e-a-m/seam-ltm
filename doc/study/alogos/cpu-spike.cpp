// cpu-spike.cpp -- does ALOGOS's heaviest setting run in real time?
// 4 faces x N Moorer all-pass sections, delays on a geometric spiral,
// 20 s of the stunedrev burst at 96 kHz in 256-sample blocks.
// Usage: ./cpu-spike            heaviest (sqrt 2, D 4, 1 m, N 119) and default (phi, D 1, 3.3302 m, N 17)
//        ./cpu-spike <sps> ...  heaviest setting cut to <sps> sections per sample (multiple of 4)
// c++ -std=c++17 -O2 -I ../../../plugins/_common -I ../../../tests cpu-spike.cpp -o cpu-spike
#include "seam_basics.h"
#include "seam_denormals.h"
#include "seam_moorer.h"
#include "seam_primes.h"
#include "stunedrev_burst.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static double delaySeconds(double r, double D, double d, int i, int k) {
    return d * std::pow(r, (i + k / 4.0) / D) / Seam::speedOfSound(Seam::kRoomCelsius);
}

static void run(const char* name, double r, double D, double d, int N, double g, const Seam::PrimeSieve& sieve) {
    const double fs = 96000.0, seconds = 20.0;
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    std::vector<std::vector<double>> arena((size_t)4 * N);
    std::vector<Seam::MoorerAllpass> sec((size_t)4 * N);
    double bytes = 0.0, longest = 0.0;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < N; ++i) {
            const double n0 = std::floor(fs * d * std::pow(r, (i + k / 4.0) / D) / c + 0.5);
            uint32_t n = (uint32_t)n0;
            if (n >= 2) n = sieve.nextPrimeAbove(n);
            if (n == 0) { std::printf("sieve bound exceeded\n"); return; }
            if (n / fs > longest) longest = n / fs;
            auto& a = arena[(size_t)k * N + i];
            a.assign((size_t)n + 1, 0.0);
            bytes += (n + 1) * sizeof(double);
            sec[(size_t)k * N + i].attach(a.data(), a.size());
            sec[(size_t)k * N + i].setDelay(n < 1 ? 1 : n);
            sec[(size_t)k * N + i].setGain(g);
        }
    const int B = 256;
    std::vector<double> ib(4 * B), ob(4 * B);
    double* in[4]; double* out[4];
    for (int ch = 0; ch < 4; ++ch) { in[ch] = ib.data() + ch * B; out[ch] = ob.data() + ch * B; }
    Burst burst(fs);
    double sink = 0.0;
    const long blocks = (long)(seconds * fs / B);
    const auto t0 = std::chrono::steady_clock::now();
    {
        Seam::ScopedNoDenormals nd;
        for (long b = 0; b < blocks; ++b) {
            burst.fill(in, B);
            for (int i = 0; i < B; ++i)
                for (int k = 0; k < 4; ++k) {
                    double x = in[k][i];
                    Seam::MoorerAllpass* s = &sec[(size_t)k * N];
                    for (int q = 0; q < N; ++q) x = s[q].tick(x);
                    out[k][i] = x;
                }
            sink += out[0][0] + out[3][B - 1];
        }
    }
    const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s: N %d (%d sections per sample), g %.4f, d %.4f m, longest delay %.2f s, "
                "arena %.1f MiB: %.1f %% of one core (%.2f s CPU for %.0f s) [sink %g]\n",
                name, N, 4 * N, g, d, longest, bytes / 1048576.0, 100.0 * cpu / seconds, cpu, seconds, sink);
}

int main(int argc, char** argv) {
    const Seam::PrimeSieve sieve(9000000u);
    const double c = Seam::speedOfSound(Seam::kRoomCelsius);
    const double s2 = std::sqrt(2.0), rho = std::pow(s2, 1.0 / 4.0);
    (void)rho;
    std::printf("c = %.14f m/s\n", c);
    for (int N : {119, 120})
        std::printf("N %d: longest (face 3, section %d) = %.3f s, %s 90 s\n", N, N - 1,
                    delaySeconds(s2, 4, 1.0, N - 1, 3), delaySeconds(s2, 4, 1.0, N - 1, 3) <= 90.0 ? "<=" : ">");
    if (argc > 1) {
        for (int a = 1; a < argc; ++a) {
            const int sps = std::atoi(argv[a]);
            run("heaviest (cut)", s2, 4, 1.0, sps / 4, 1.0 / s2, sieve);
        }
        return 0;
    }
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    run("heaviest", s2, 4, 1.0, 119, 1.0 / s2, sieve);
    run("default ", phi, 1, std::pow(phi, 10.0 / 4.0), 17, 1.0 / phi, sieve);
}
