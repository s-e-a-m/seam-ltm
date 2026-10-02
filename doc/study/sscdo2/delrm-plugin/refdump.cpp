// refdump.cpp -- render a delRM reference DSP and print it as C++ arrays,
// doubles in hex-float (exact to the bit). Used by gen-ref.sh only.
//
//   refdump SR SECONDS NAME [AT LABEL VALUE]
//
// The signal (tests/delrm_signal.h) feeds the DSP's inputs (1 or 4) in
// blocks of 256. Window w of output c is the 512 samples from w*SR/8, and
// NAME_energy[c][w] the energy of output c over [w*SR/8, (w+1)*SR/8). With
// AT (a multiple of 256), the entry LABEL is set to VALUE before the block
// starting at sample AT, as the C++ test sets the distance at that block.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#define FAUSTFLOAT double
#include "delrm_signal.h"

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
    void addHorizontalBargraph(const char*, double*, double, double) {}
    void addVerticalBargraph(const char*, double*, double, double) {}
};
struct dsp { virtual ~dsp() {} };
#include "ref.h"

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "see the header of refdump.cpp\n"); return 2; }
    const int sr = atoi(argv[1]);
    const int seconds = atoi(argv[2]);
    const char* name = argv[3];
    const long at = argc >= 7 ? atol(argv[4]) : -1;
    const char* label = argc >= 7 ? argv[5] : nullptr;
    const double value = argc >= 7 ? atof(argv[6]) : 0.0;

    Ref* d = new Ref;
    d->init(sr);
    UI ui; d->buildUserInterface(&ui);
    const int ni = d->getNumInputs(), no = d->getNumOutputs();
    if ((ni != 1 && ni != 4) || no < 1 || no > 4 || (at >= 0 && at % 256 != 0) || sr % 8 != 0) {
        fprintf(stderr, "bad shape, AT or SR\n"); return 2;
    }
    if (at >= 0 && !ui.zones.count(label)) { fprintf(stderr, "no entry %s\n", label); return 2; }

    const int B = 256, W = 512;
    const long period = sr / 8;
    const int nwin = seconds * 8;
    std::vector<double> ib(4 * B), ob(4 * B);
    double* ip[4]; double* op[4];
    for (int c = 0; c < 4; ++c) { ip[c] = ib.data() + c * B; op[c] = ob.data() + c * B; }
    std::vector<double> win((size_t)no * nwin * W, 0.0), en((size_t)no * nwin, 0.0);
    DelrmSignal sig((double)sr);
    const long total = (long)sr * seconds;
    for (long pos = 0; pos < total; pos += B) {
        const int m = (int)(total - pos < B ? total - pos : B);
        if (pos == at) *ui.zones[label] = value;
        sig.fill(ip, m, ni);
        d->compute(m, ip, op);
        for (int c = 0; c < no; ++c)
            for (int k = 0; k < m; ++k) {
                const long g = pos + k; const int w = (int)(g / period); const long off = g % period;
                const double y = op[c][k];
                if (off < W) win[((size_t)c * nwin + w) * W + off] = y;
                en[(size_t)c * nwin + w] += y * y;
            }
    }
    printf("// %s: %d Hz, %d s", name, sr, seconds);
    if (at >= 0) printf(", %s = %g from sample %ld", label, value, at);
    printf("\nstatic const double %s[%d][%d][%d] = {\n", name, no, nwin, W);
    for (int c = 0; c < no; ++c) {
        printf("{");
        for (int w = 0; w < nwin; ++w) {
            printf("{");
            for (int k = 0; k < W; ++k)
                printf("%a%s", win[((size_t)c * nwin + w) * W + k], k + 1 < W ? "," : "");
            printf("}%s", w + 1 < nwin ? "," : "");
        }
        printf("}%s\n", c + 1 < no ? "," : "");
    }
    printf("};\nstatic const double %s_energy[%d][%d] = {\n", name, no, nwin);
    for (int c = 0; c < no; ++c) {
        printf("{");
        for (int w = 0; w < nwin; ++w) printf("%a%s", en[(size_t)c * nwin + w], w + 1 < nwin ? "," : "");
        printf("}%s\n", c + 1 < no ? "," : "");
    }
    printf("};\n");
    return 0;
}
