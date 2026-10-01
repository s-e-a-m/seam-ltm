// refdump.cpp -- render a Faust DSP offline and print its outputs as a C++
// array of hex-float doubles, exact to the bit. Used by gen-ref.sh only.
//
// usage: refdump SR NSAMPLES NAME impulse|zero
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define FAUSTFLOAT double
struct Meta { virtual ~Meta() {} virtual void declare(const char*, const char*) {} };
struct UI {
    void openVerticalBox(const char*) {} void openHorizontalBox(const char*) {}
    void closeBox() {} void declare(double*, const char*, const char*) {}
    void addHorizontalSlider(const char*, double*, double, double, double, double) {}
    void addVerticalSlider(const char*, double*, double, double, double, double) {}
    void addNumEntry(const char*, double*, double, double, double, double) {}
};
struct dsp { virtual ~dsp() {} };
#include "ref.h"

int main(int argc, char** argv) {
    if (argc != 5) { fprintf(stderr, "usage: refdump SR N NAME impulse|zero\n"); return 2; }
    const int sr = atoi(argv[1]); const int n = atoi(argv[2]);
    const bool imp = strcmp(argv[4], "impulse") == 0;
    Ref d; d.init(sr);   // classInit + instanceInit: state zeroed, as a fresh instance
    const int ni = d.getNumInputs(), no = d.getNumOutputs();
    std::vector<std::vector<double>> in(ni ? ni : 1, std::vector<double>(n, 0.0));
    std::vector<std::vector<double>> out(no, std::vector<double>(n, 0.0));
    if (imp && ni) in[0][0] = 1.0;
    std::vector<double*> ip, op;
    for (auto& v : in) ip.push_back(v.data());
    for (auto& v : out) op.push_back(v.data());
    d.compute(n, ip.data(), op.data());
    printf("// %s: %d outputs x %d samples at %d Hz\n", argv[3], no, n, sr);
    printf("static const double %s[%d][%d] = {\n", argv[3], no, n);
    for (int c = 0; c < no; ++c) {
        printf("{");
        for (int k = 0; k < n; ++k) printf("%a%s", out[c][k], k + 1 < n ? "," : "");
        printf("}%s\n", c + 1 < no ? "," : "");
    }
    printf("};\n");
    return 0;
}
