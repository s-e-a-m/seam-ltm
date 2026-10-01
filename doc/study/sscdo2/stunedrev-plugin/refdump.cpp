// refdump.cpp -- render the stunedrev specification and print it as C++
// arrays, doubles in hex-float (exact to the bit). Used by gen-ref.sh only.
//
//   refdump stdel   SR NAME                    delays for ms = 1..100 -> int NAME[100][outputs]
//   refdump impulse SR N NAME                  impulse response of output 0 -> NAME[N]
//   refdump windows SR SECONDS NAME [AT LABEL VALUE]
//       the burst (tests/stunedrev_burst.h) into the four inputs, in blocks
//       of 256; NAME[4][SECONDS][512] holds the first 512 samples of every
//       second, NAME_energy[4][SECONDS] the energy of every second. With AT,
//       the entry LABEL is set to VALUE before the block starting at sample
//       AT (a multiple of 256), as the C++ test sets the time at that block.
//
// The DSP is heap-allocated: the spec sizes its buffers at the 192 kHz
// bound of ma.SR, about 1.7 GiB.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define FAUSTFLOAT double
#include "stunedrev_burst.h"

struct Meta { virtual ~Meta() {} virtual void declare(const char*, const char*) {} };
struct UI {
    std::map<std::string, double*> zones;
    void openVerticalBox(const char*) {} void openHorizontalBox(const char*) {}
    void openTabBox(const char*) {} void closeBox() {}
    void declare(double*, const char*, const char*) {}
    void addHorizontalSlider(const char* l, double* z, double, double, double, double) { zones[l] = z; }
    void addVerticalSlider(const char* l, double* z, double, double, double, double) { zones[l] = z; }
    void addNumEntry(const char* l, double* z, double, double, double, double) { zones[l] = z; }
    void addButton(const char* l, double* z) { zones[l] = z; }
    void addCheckButton(const char* l, double* z) { zones[l] = z; }
};
struct dsp { virtual ~dsp() {} };
#include "ref.h"

static double* zone(UI& ui, const char* label) {
    auto it = ui.zones.find(label);
    if (it == ui.zones.end()) { fprintf(stderr, "no entry %s\n", label); exit(2); }
    return it->second;
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "see the header of refdump.cpp\n"); return 2; }
    const std::string mode = argv[1];
    const int sr = atoi(argv[2]);
    Ref* d = new Ref;
    d->init(sr);
    UI ui; d->buildUserInterface(&ui);
    const int ni = d->getNumInputs(), no = d->getNumOutputs();

    if (mode == "stdel") {
        std::vector<double> o((size_t)no);
        std::vector<double*> op; for (auto& v : o) op.push_back(&v);
        printf("// %s: sdt.stdel at %d Hz, row = ms - 1, column = line*42 + section\n", argv[3], sr);
        printf("static const int %s[100][%d] = {\n", argv[3], no);
        for (int ms = 1; ms <= 100; ++ms) {
            *zone(ui, "ms") = ms;
            d->compute(1, nullptr, op.data());
            printf("{");
            for (int c = 0; c < no; ++c) printf("%d%s", (int)o[(size_t)c], c + 1 < no ? "," : "");
            printf("}%s\n", ms < 100 ? "," : "");
        }
        printf("};\n");
        return 0;
    }

    if (mode == "impulse") {
        const int n = atoi(argv[3]);
        std::vector<double> in((size_t)n, 0.0), out((size_t)n, 0.0);
        in[0] = 1.0;
        double* ip[1] = { in.data() }; double* op[1] = { out.data() };
        d->compute(n, ip, op);
        printf("// %s: impulse response, %d samples at %d Hz\n", argv[4], n, sr);
        printf("static const double %s[%d] = {", argv[4], n);
        for (int k = 0; k < n; ++k) printf("%a%s", out[(size_t)k], k + 1 < n ? "," : "");
        printf("};\n");
        return 0;
    }

    if (mode == "windows") {
        const int seconds = atoi(argv[3]);
        const char* name = argv[4];
        const long at = argc >= 8 ? atol(argv[5]) : -1;
        const char* label = argc >= 8 ? argv[6] : nullptr;
        const double value = argc >= 8 ? atof(argv[7]) : 0.0;
        if (ni != 4 || no != 4 || (at >= 0 && at % 256 != 0)) { fprintf(stderr, "bad shape or AT\n"); return 2; }
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* ip[4]; double* op[4];
        for (int c = 0; c < 4; ++c) { ip[c] = ib.data() + c * B; op[c] = ob.data() + c * B; }
        std::vector<double> win((size_t)4 * seconds * 512, 0.0), en((size_t)4 * seconds, 0.0);
        Burst burst((double)sr);
        const long total = (long)sr * seconds;
        for (long pos = 0; pos < total; pos += B) {
            const int m = (int)(total - pos < B ? total - pos : B);
            if (pos == at) *zone(ui, label) = value;
            burst.fill(ip, m);
            d->compute(m, ip, op);
            for (int c = 0; c < 4; ++c)
                for (int k = 0; k < m; ++k) {
                    const long g = pos + k; const int s = (int)(g / sr); const long off = g % sr;
                    const double y = op[c][k];
                    if (off < 512) win[((size_t)c * seconds + s) * 512 + off] = y;
                    en[(size_t)c * seconds + s] += y * y;
                }
        }
        printf("// %s: sdt.stunedrev on the burst at %d Hz, %d s", name, sr, seconds);
        if (at >= 0) printf(", %s = %g from sample %ld", label, value, at);
        printf("\nstatic const double %s[4][%d][512] = {\n", name, seconds);
        for (int c = 0; c < 4; ++c) {
            printf("{");
            for (int s = 0; s < seconds; ++s) {
                printf("{");
                for (int k = 0; k < 512; ++k)
                    printf("%a%s", win[((size_t)c * seconds + s) * 512 + k], k < 511 ? "," : "");
                printf("}%s", s + 1 < seconds ? "," : "");
            }
            printf("}%s\n", c < 3 ? "," : "");
        }
        printf("};\nstatic const double %s_energy[4][%d] = {\n", name, seconds);
        for (int c = 0; c < 4; ++c) {
            printf("{");
            for (int s = 0; s < seconds; ++s) printf("%a%s", en[(size_t)c * seconds + s], s + 1 < seconds ? "," : "");
            printf("}%s\n", c < 3 ? "," : "");
        }
        printf("};\n");
        return 0;
    }
    fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
