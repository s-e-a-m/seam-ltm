// arch.cpp -- offline Faust architecture for the LMO band-filter study.
//
// Adapted from the SSCDO#2 audit harness. One mono (or zero-input) DSP,
// run offline, output written as raw little-endian float64.
//
// usage: prog SR NSAMPLES [key=value ...]
//   in=impulse | zero | tone:FREQ:AMP | file:PATH     input signal (file = raw float32)
//   out=PATH                                          raw float64 output (omit: no output)
//   pre=SECONDS                                       run this long first, output discarded
//                                                     (the input file is consumed during it)
//   bench=REPS                                        time REPS runs of NSAMPLES; prints ns/sample
//   any other key=value                               sets the DSP parameter of that name
//
// The same float32 input file feeds -single and -double builds, so the two
// precisions see bit-identical input.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include "faust/dsp/dsp.h"
#include "faust/gui/MapUI.h"
#include "faust/gui/meta.h"
<<includeIntrinsic>>
<<includeclass>>

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s SR NSAMPLES [key=value ...]\n", argv[0]); return 2; }
  int sr = atoi(argv[1]); long ns = atol(argv[2]);
  std::string in = "zero", out; double pre = 0; int bench = 0;
  std::vector<std::pair<std::string,double>> params;
  for (int a = 3; a < argc; a++) {
    std::string s(argv[a]); auto p = s.find('=');
    if (p == std::string::npos) { fprintf(stderr, "bad arg %s\n", s.c_str()); return 2; }
    std::string k = s.substr(0, p), v = s.substr(p + 1);
    if (k == "in") in = v; else if (k == "out") out = v;
    else if (k == "pre") pre = atof(v.c_str()); else if (k == "bench") bench = atoi(v.c_str());
    else params.push_back({k, atof(v.c_str())});
  }
  // on the heap: a DSP with large delay lines as members overflows the stack
  mydsp& d = *new mydsp; d.init(sr); MapUI ui; d.buildUserInterface(&ui);
  for (auto& kv : params) {
    // reject names the DSP does not have, so a typo cannot pass silently
    if (!ui.getLabelMap().count(kv.first) && !ui.getShortnameMap().count(kv.first) && !ui.getFullpathMap().count(kv.first)) {
      fprintf(stderr, "unknown parameter %s\n", kv.first.c_str()); return 2; }
    ui.setParamValue(kv.first, kv.second);
  }
  int ni = d.getNumInputs(), no = d.getNumOutputs();
  if (ni > 1 || no != 1) { fprintf(stderr, "expects <=1 input, 1 output (got %d,%d)\n", ni, no); return 2; }
  const int B = 256;
  std::vector<FAUSTFLOAT> ib(B, 0), ob(B, 0); FAUSTFLOAT* ip = ib.data(); FAUSTFLOAT* op = ob.data();

  // input source
  FILE* fin = nullptr; double tf = 0, ta = 0; bool imp = false;
  if (in == "impulse") imp = true;
  else if (in.rfind("tone:", 0) == 0) { sscanf(in.c_str() + 5, "%lf:%lf", &tf, &ta); }
  else if (in.rfind("file:", 0) == 0) { fin = fopen(in.c_str() + 5, "rb"); if (!fin) { perror("input"); return 2; } }
  else if (in != "zero") { fprintf(stderr, "bad in=%s\n", in.c_str()); return 2; }
  long gpos = 0; // global sample index (pre-roll included)
  auto fill = [&](int n) {
    if (fin) { std::vector<float> t(n); size_t r = fread(t.data(), sizeof(float), n, fin);
      if ((int)r != n) { fprintf(stderr, "input file too short\n"); exit(3); }
      for (int k = 0; k < n; k++) ib[k] = (FAUSTFLOAT)t[k]; }
    else for (int k = 0; k < n; k++) {
      long g = gpos + k;
      ib[k] = imp ? (g == 0 ? 1 : 0) : (ta != 0 ? (FAUSTFLOAT)(ta * sin(2 * M_PI * tf * (double)g / sr)) : 0);
    }
  };

  if (bench > 0) {
    std::vector<double> t;
    for (int r = 0; r < bench; r++) {
      if (fin) fseek(fin, 0, SEEK_SET);
      gpos = 0; d.instanceClear();
      std::vector<std::vector<FAUSTFLOAT>> allin; // pre-read input so file I/O is not timed
      long nb = (ns + B - 1) / B; allin.resize(nb);
      for (long b = 0; b < nb; b++) { fill(B); allin[b] = ib; gpos += B; }
      auto st = std::chrono::steady_clock::now();
      double sink = 0;
      for (long b = 0; b < nb; b++) { FAUSTFLOAT* p = allin[b].data(); d.compute(B, ni ? &p : nullptr, &op); sink += op[B - 1]; }
      auto en = std::chrono::steady_clock::now();
      t.push_back(std::chrono::duration<double, std::nano>(en - st).count() / (nb * B));
      if (sink == 12345.678) printf("!");
    }
    std::sort(t.begin(), t.end());
    printf("bench ns_per_sample median=%.4f min=%.4f max=%.4f reps=%d\n", t[t.size() / 2], t.front(), t.back(), bench);
    return 0;
  }

  FILE* fo = out.empty() ? nullptr : fopen(out.c_str(), "wb");
  if (!out.empty() && !fo) { perror("output"); return 2; }
  long npre = (long)llround(pre * sr);
  std::vector<double> obd(B);
  for (long n = 0; n < npre + ns; n += B) {
    int m = (int)std::min<long>(B, npre + ns - n);
    fill(m); d.compute(m, ni ? &ip : nullptr, &op); gpos += m;
    if (fo) {
      int s0 = (int)std::max<long>(0, npre - n); int c = 0;
      for (int k = s0; k < m; k++) obd[c++] = (double)ob[k];
      if (c) fwrite(obd.data(), sizeof(double), c, fo);
    }
  }
  if (fo) fclose(fo);
  return 0;
}
