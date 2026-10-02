#!/usr/bin/env python3
# mutate.py -- apply each mutation of mutations.md, rebuild its test in build-test,
# run it, restore the source. Run from the repository root:
#   python3 doc/study/sscdo2/stunedrev-plugin/mutate.py [index ...]
import subprocess, sys
P='plugins/_common/seam_primes.h'; M='plugins/_common/seam_moorer.h'
D='plugins/stunedrev/source/stunedrev_dsp.h'; PA='plugins/stunedrev/source/stunedrev_params.h'; ST='plugins/stunedrev/source/stunedrev_state.h'
MUT=[
 ("seam_primes_test","nextPrimeAbove starts at n when n is odd (not strictly greater)",P,"(n & 1) ? (uint64_t)n + 2 : (uint64_t)n + 1","(n & 1) ? (uint64_t)n : (uint64_t)n + 1"),
 ("seam_primes_test","sieve inner step p instead of 2p",P,"m += 2 * p","m += p"),
 ("stunedrev_dsp_test","floor(ms*fs/1000) without +0.5 (truncation, Davide's rule)",P,"std::floor(ms * fs / 1000.0 + 0.5)","std::floor(ms * fs / 1000.0)"),
 ("stunedrev_dsp_test","phi written 1.618",D,"(1.0 + std::sqrt(5.0)) / 2.0","1.618"),
 ("seam_moorer_test","+g instead of -g",M,"const double a = -g_ * (x - v_);","const double a = g_ * (x - v_);"),
 ("seam_moorer_test","read t back instead of t-1",M,"pos_ + len_ - (t_ - 1)","pos_ + len_ - t_"),
 ("seam_moorer_test","clear() keeps v",M,"void clear() { pos_ = 0; v_ = 0.0; }","void clear() { pos_ = 0; }"),
 ("stunedrev_dsp_test","sectionLength without +1",D,"sectionDelay(line, i, kTMax, fs, s) + 1;","sectionDelay(line, i, kTMax, fs, s);"),
 ("stunedrev_dsp_test","ratios e and pi swapped",D,"2.718281828459045, 3.141592653589793 }","3.141592653589793, 2.718281828459045 }"),
 ("stunedrev_dsp_test","the loop over sections stops at 41",D,"for (Section& s : sec_[j]) y = s.tick(y);","for (int i = 0; i < kSections - 1; ++i) y = sec_[j][i].tick(y);"),
 ("stunedrev_dsp_test","setTime stores the time, applies it only in prepare",D,"if (arena_) applyTime(line);","if (false) applyTime(line);"),
 ("stunedrev_dsp_test","outputs zeroed at the start of the block (in-place hazard)",D,"        const bool feeding = phase_ == Phase::Run;\n","        zero(out, n);\n        const bool feeding = phase_ == Phase::Run;\n"),
 ("stunedrev_dsp_test","setGain(kG) omitted in prepare (g = 0)",D,"sec_[j][i].setGain(kG);",""),
 ("stunedrev_dsp_test","attach() does not clear the state (prepare at a new rate)",M,"len_ = len; clear(); }","len_ = len; }"),
 ("stunedrev_dsp_test","centroid divided by 96000 instead of fs",D,"centroid_[line].store(sum / fs_);","centroid_[line].store(sum / 96000.0);"),
 ("stunedrev_dsp_test","setPower ignored",D,"if (t != pow_.target()) pow_.setTarget(t, kShortRamp, fs_);","(void)t;"),
 ("stunedrev_dsp_test","RESET: a chunk skipped",D,"clearPos_ += m;","clearPos_ += m + want;"),
 ("stunedrev_dsp_test","RESET: sections not cleared at the end",D,"for (auto& line : sec_) for (Section& s : line) s.clear();",""),
 ("stunedrev_dsp_test","RESET: a click during the clearing is deferred",D,"if (gen != servedGen_) {","if (gen != servedGen_ && phase_ == Phase::Run) {"),
 ("stunedrev_dsp_test","RESET: output buffer not zeroed during the clearing",D,"            zero(out, n);\n            return;","            return;"),
 ("stunedrev_dsp_test","RESET: a click before prepare is replayed",D,"        servedGen_ = resetGen_.load();       // a click before prepare is not replayed\n",""),
 ("stunedrev_params_test","lround(norm*99) instead of the SDK's int(norm*100)",PA,"kTMin + std::min(kTimeSteps, (int)(norm * (kTimeSteps + 1)));","kTMin + (int)std::lround(norm * kTimeSteps);"),
 ("stunedrev_state_test","readState does not store what it read",ST,"for (int i = 0; i < kNumParams; ++i) box.store((Param)i, v[i]);",""),
]
only = sys.argv[1:] and set(map(int, sys.argv[1:]))
rows=[]
for i,(test,desc,f,a,b) in enumerate(MUT):
    if only and i not in only: continue
    s=open(f).read(); assert s.count(a)==1,(i,a)
    open(f,'w').write(s.replace(a,b))
    try:
        bld=subprocess.run(["cmake","--build","build-test","--config","Release","--target",test],capture_output=True,text=True)
        if bld.returncode: res="RED (does not compile)"
        else:
            r=subprocess.run(["ctest","--test-dir","build-test","-C","Release","-R","^%s$"%test,"--timeout","600"],capture_output=True,text=True)
            res="GREEN" if r.returncode==0 else "RED"
    finally:
        subprocess.run(["git","checkout","--",f])
    print(i,test,"|",desc,"|",res,flush=True); rows.append((test,desc,res))
# The restored sources must leave restored binaries: rebuild every test touched.
for t in sorted({m[0] for i, m in enumerate(MUT) if not only or i in only}):
    subprocess.run(["cmake", "--build", "build-test", "--config", "Release", "--target", t], capture_output=True)
