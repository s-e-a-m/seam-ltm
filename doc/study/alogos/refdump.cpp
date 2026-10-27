// refdump.cpp -- render the alogos specification (seam.silvi.lib) and print it
// as C++ arrays, doubles in hex-float (exact to the bit). Used by gen-ref.sh only.
//
//   refdump winj SCALE D                 print j = round(4 D log_r(3.33)) clamped
//                                        to 0..J (id 1, u = 0); needs no DSP
//   refdump grid SR                      kJ, kAlgrid, kNmax (aldel.dsp)
//   refdump aldel SR TAG                 kAldelHash<TAG>, kAldelEdge*<TAG> (aldel.dsp)
//   refdump windows SR SECONDS NAME      the burst through alogos.dsp
//
// SCALE is one of phi, sqrt2, e, pi; scale index 0..3 in that order. Density
// index 0..2 = D 1, 2, 4. ids 0..4. Grid j 0..J(r, D), J <= 81, 82 slots.
//
// N_max (the Global Constraints rule, not a Faust function): the largest
// N >= 1 with d * pow(r, (N - 1 + 3/4)/D) / c <= 90.0 s, capped at 128, where
// d is the spec's grid value algrid(r, D, id, j) and c = 343.21462268256795.
//
// Hash: 64-bit FNV-1a (offset 0xcbf29ce484222325, prime 0x100000001b3) over
// the delays as uint32 little-endian bytes, k-major: face 0 sections
// 0..N_max-1, then face 1, 2, 3.
//
// windows: the burst (tests/stunedrev_burst.h) into the four inputs, in
// blocks of 256; NAME[4][SECONDS][512] holds the first 512 samples of every
// second, NAME_energy[4][SECONDS] the energy of every second.
//
// The DSP is heap-allocated: the spec sizes its buffers at 192 kHz and 6 m.
#include <cinttypes>
#include <cmath>
#include <cstdint>
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

static const char* kScaleName[4] = { "phi", "sqrt2", "e", "pi" };
static const double kScale[4] = { (1.0 + std::sqrt(5.0)) / 2.0, std::sqrt(2.0), std::exp(1.0), M_PI };
static const int kDensity[3] = { 1, 2, 4 };
static const int kJExpected[4][3] = { { 14, 28, 58 }, { 19, 40, 81 }, { 6, 13, 27 }, { 5, 11, 24 } };
static const double kC = 343.21462268256795;
enum { S = 4, ND = 3, NID = 5, NJ = 82, NMAX = 128 };

static int lastJ(double r, int D) {
    int j = 0;
    while (std::pow(r, (j + 1 + 7.0 / 8.0) / (4.0 * D)) <= 6.0) ++j;
    return j;
}

static int nMax(double r, int D, double d) {
    int n = 1;
    while (n < NMAX && d * std::pow(r, (n + 3.0 / 4.0) / D) / kC <= 90.0) ++n;
    return n;
}

static int scaleIndex(const char* s) {
    for (int q = 0; q < S; ++q) if (!strcmp(s, kScaleName[q])) return q;
    fprintf(stderr, "unknown scale %s\n", s); exit(2);
}

static void checkJ() {
    for (int q = 0; q < S; ++q)
        for (int dn = 0; dn < ND; ++dn)
            if (lastJ(kScale[q], kDensity[dn]) != kJExpected[q][dn]) {
                fprintf(stderr, "J(%s, %d) = %d, expected %d\n", kScaleName[q], kDensity[dn],
                        lastJ(kScale[q], kDensity[dn]), kJExpected[q][dn]);
                exit(2);
            }
}

// The spec's grid value algrid(r, D, id, j) read through the entries.
struct Spec {
    Ref* dsp; UI ui; double o[6]; double* op[6];
    explicit Spec(int sr) : dsp(new Ref) {
        dsp->init(sr); dsp->buildUserInterface(&ui);
        if (dsp->getNumOutputs() != 6) { fprintf(stderr, "bad shape\n"); exit(2); }
        for (int c = 0; c < 6; ++c) op[c] = &o[c];
    }
    void run() { dsp->compute(1, nullptr, op); }
    double grid(double r, int D, int id, int j) {
        *zone(ui, "r") = r; *zone(ui, "D") = D; *zone(ui, "j") = j; run(); return o[id];
    }
    uint32_t del(double r, int D, double d, int i, int k) {
        *zone(ui, "r") = r; *zone(ui, "D") = D; *zone(ui, "d") = d;
        *zone(ui, "i") = i; *zone(ui, "k") = k; run(); return (uint32_t)o[5];
    }
};

// The entries carry the exact doubles: pow(r, 4D/(4D)) is r itself, so the
// grid value at id 1, j = 4D must be bit-identical to the scale we wrote.
static void checkEntries(Spec& s) {
    for (int q = 0; q < S; ++q)
        for (int dn = 0; dn < ND; ++dn) {
            const int D = kDensity[dn];
            const double g = s.grid(kScale[q], D, 1, 4 * D);
            if (std::memcmp(&g, &kScale[q], sizeof g)) {
                fprintf(stderr, "entry quantised: %s D %d: %a != %a\n", kScaleName[q], D, g, kScale[q]);
                exit(2);
            }
        }
}

static uint64_t fnv1a(const std::vector<uint32_t>& v) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint32_t x : v)
        for (int b = 0; b < 4; ++b) { h ^= (x >> (8 * b)) & 0xffu; h *= 0x100000001b3ULL; }
    return h;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "see the header of refdump.cpp\n"); return 2; }
    const std::string mode = argv[1];
    checkJ();

    if (mode == "winj") {
        const int q = scaleIndex(argv[2]);
        const int D = atoi(argv[3]);
        const double r = kScale[q];
        long j = std::lround(4.0 * D * std::log(3.33) / std::log(r));
        const int J = lastJ(r, D);
        if (j < 0) j = 0;
        if (j > J) j = J;
        printf("%ld\n", j);
        return 0;
    }

    const int sr = atoi(argv[2]);

    if (mode == "grid") {
        Spec s(sr);
        checkEntries(s);
        double maxdev = 0.0;
        std::vector<double> grid((size_t)S * ND * NID * NJ, 0.0);
        std::vector<int> nm((size_t)S * ND * NID * NJ, 0);
        for (int q = 0; q < S; ++q)
            for (int dn = 0; dn < ND; ++dn) {
                const int D = kDensity[dn], J = lastJ(kScale[q], D);
                for (int j = 0; j <= J; ++j) {
                    *zone(s.ui, "r") = kScale[q]; *zone(s.ui, "D") = D; *zone(s.ui, "j") = j; s.run();
                    for (int id = 0; id < NID; ++id) {
                        const size_t x = (((size_t)q * ND + dn) * NID + id) * NJ + j;
                        grid[x] = s.o[id];
                        nm[x] = nMax(kScale[q], D, s.o[id]);
                        static const double u[5] = { 0, 0, 0.5, 0.25, 0.75 };
                        const double c = std::pow(kScale[q], (j + u[id]) / (4.0 * D));
                        maxdev = std::fmax(maxdev, std::fabs(c / s.o[id] - 1.0));
                    }
                }
            }
        fprintf(stderr, "grid: max relative deviation Faust vs C++ pow: %g\n", maxdev);
        printf("// kJ[scale][density]: the last grid point J(r, D).\n");
        printf("constexpr int kJ[4][3] = {");
        for (int q = 0; q < S; ++q) {
            printf("{");
            for (int dn = 0; dn < ND; ++dn) printf("%d%s", lastJ(kScale[q], kDensity[dn]), dn < ND - 1 ? "," : "");
            printf("}%s", q < S - 1 ? "," : "");
        }
        printf("};\n");
        printf("// kAlgrid[scale][density][id][j]: sgs.algrid(r, D, id, j) in metres; 0 for j > J.\n");
        printf("constexpr double kAlgrid[4][3][5][82] = {\n");
        for (int q = 0; q < S; ++q) {
            printf("{");
            for (int dn = 0; dn < ND; ++dn) {
                printf("{");
                for (int id = 0; id < NID; ++id) {
                    printf("{");
                    for (int j = 0; j < NJ; ++j)
                        printf("%a%s", grid[(((size_t)q * ND + dn) * NID + id) * NJ + j], j < NJ - 1 ? "," : "");
                    printf("}%s", id < NID - 1 ? "," : "");
                }
                printf("}%s\n", dn < ND - 1 ? "," : "");
            }
            printf("}%s\n", q < S - 1 ? "," : "");
        }
        printf("};\n");
        printf("// kNmax[scale][density][id][j]: N_max at kAlgrid (rate-independent); 0 for j > J.\n");
        printf("constexpr int kNmax[4][3][5][82] = {\n");
        for (int q = 0; q < S; ++q) {
            printf("{");
            for (int dn = 0; dn < ND; ++dn) {
                printf("{");
                for (int id = 0; id < NID; ++id) {
                    printf("{");
                    for (int j = 0; j < NJ; ++j)
                        printf("%d%s", nm[(((size_t)q * ND + dn) * NID + id) * NJ + j], j < NJ - 1 ? "," : "");
                    printf("}%s", id < NID - 1 ? "," : "");
                }
                printf("}%s\n", dn < ND - 1 ? "," : "");
            }
            printf("}%s\n", q < S - 1 ? "," : "");
        }
        printf("};\n");
        return 0;
    }

    if (mode == "aldel") {
        const char* tag = argv[3];
        Spec s(sr);
        checkEntries(s);
        std::vector<uint64_t> hash((size_t)S * ND * NID * NJ, 0);
        std::vector<int> enm((size_t)S * ND * NID * 2, 0), eoff((size_t)S * ND * NID * 2, 0);
        std::vector<uint32_t> edge;
        long count = 0, differ = 0;
        for (int q = 0; q < S; ++q)
            for (int dn = 0; dn < ND; ++dn) {
                const double r = kScale[q];
                const int D = kDensity[dn], J = lastJ(r, D);
                for (int id = 0; id < NID; ++id)
                    for (int j = 0; j <= J; ++j) {
                        const double d = s.grid(r, D, id, j);
                        const int n = nMax(r, D, d);
                        std::vector<uint32_t> v((size_t)4 * n);
                        for (int k = 0; k < 4; ++k)
                            for (int i = 0; i < n; ++i) {
                                const uint32_t x = s.del(r, D, d, i, k);
                                v[(size_t)k * n + i] = x;
                                // Diagnostic: the Global Constraints form, in its own order.
                                const int m = (int)std::floor(sr * d * std::pow(r, (i + k / 4.0) / D) / kC + 0.5);
                                if ((uint32_t)(m < 2 ? m : next_pr(m)) != x) ++differ;
                            }
                        count += 4 * n;
                        hash[(((size_t)q * ND + dn) * NID + id) * NJ + j] = fnv1a(v);
                        const int side = j == 0 ? 0 : (j == J ? 1 : -1);
                        if (side >= 0) {
                            const size_t e = (((size_t)q * ND + dn) * NID + id) * 2 + side;
                            enm[e] = n; eoff[e] = (int)edge.size();
                            edge.insert(edge.end(), v.begin(), v.end());
                        }
                    }
            }
        fprintf(stderr, "aldel %d Hz: %ld delays, %ld differ from floor(fs d pow / c + 0.5)\n", sr, count, differ);
        printf("// kAldelHash%s[scale][density][id][j]: FNV-1a 64 of the delay list at %d Hz; 0 for j > J.\n", tag, sr);
        printf("constexpr uint64_t kAldelHash%s[4][3][5][82] = {\n", tag);
        for (int q = 0; q < S; ++q) {
            printf("{");
            for (int dn = 0; dn < ND; ++dn) {
                printf("{");
                for (int id = 0; id < NID; ++id) {
                    printf("{");
                    for (int j = 0; j < NJ; ++j)
                        printf("0x%016" PRIx64 "ULL%s", hash[(((size_t)q * ND + dn) * NID + id) * NJ + j], j < NJ - 1 ? "," : "");
                    printf("}%s\n", id < NID - 1 ? "," : "");
                }
                printf("}%s\n", dn < ND - 1 ? "," : "");
            }
            printf("}%s\n", q < S - 1 ? "," : "");
        }
        printf("};\n");
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<int>& t = pass ? eoff : enm;
            printf(pass ? "// kAldelEdgeOff%s[scale][density][id][side]: offset of the list in kAldelEdge%s.\n"
                        : "// kAldelEdgeNmax%s[scale][density][id][side]: N_max; side 0 = j 0, side 1 = j J (%s).\n",
                   tag, pass ? tag : "rate-independent");
            printf("constexpr int kAldelEdge%s%s[4][3][5][2] = {\n", pass ? "Off" : "Nmax", tag);
            for (int q = 0; q < S; ++q) {
                printf("{");
                for (int dn = 0; dn < ND; ++dn) {
                    printf("{");
                    for (int id = 0; id < NID; ++id) {
                        const size_t e = (((size_t)q * ND + dn) * NID + id) * 2;
                        printf("{%d,%d}%s", t[e], t[e + 1], id < NID - 1 ? "," : "");
                    }
                    printf("}%s", dn < ND - 1 ? "," : "");
                }
                printf("}%s\n", q < S - 1 ? "," : "");
            }
            printf("};\n");
        }
        printf("// kAldelEdge%s: the delay lists at %d Hz, list e at kAldelEdgeOff%s[e],\n"
               "// 4 * kAldelEdgeNmax%s[e] values, [k * N_max + i].\n", tag, sr, tag, tag);
        printf("constexpr uint32_t kAldelEdge%s[%zu] = {\n", tag, edge.size());
        for (size_t x = 0; x < edge.size(); ++x)
            printf("%u%s", edge[x], x + 1 < edge.size() ? ((x + 1) % 32 ? "," : ",\n") : "\n");
        printf("};\n");
        return 0;
    }

    if (mode == "windows") {
        const int seconds = atoi(argv[3]);
        const char* name = argv[4];
        Ref* d = new Ref;
        d->init(sr);
        if (d->getNumInputs() != 4 || d->getNumOutputs() != 4) { fprintf(stderr, "bad shape\n"); return 2; }
        const int B = 256;
        std::vector<double> ib(4 * B), ob(4 * B);
        double* ip[4]; double* op[4];
        for (int c = 0; c < 4; ++c) { ip[c] = ib.data() + c * B; op[c] = ob.data() + c * B; }
        std::vector<double> win((size_t)4 * seconds * 512, 0.0), en((size_t)4 * seconds, 0.0);
        Burst burst((double)sr);
        const long total = (long)sr * seconds;
        for (long pos = 0; pos < total; pos += B) {
            const int m = (int)(total - pos < B ? total - pos : B);
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
        printf("static const double %s[4][%d][512] = {\n", name, seconds);
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
