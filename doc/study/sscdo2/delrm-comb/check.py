# check.py -- run from build/ by run.sh. Every check prints ok or WRONG; mutations must FAIL.
import math, subprocess
import numpy as np

ok = True
def expect(name, errs, should_fail=False):
    global ok
    good = bool(errs) == should_fail
    ok &= good
    print(f"  [{'ok' if good else 'WRONG'}] {name}: {'FAIL' if errs else 'pass'}{' (expected FAIL)' if should_fail else ''} {'; '.join(errs)}")

def run(p, sr, n, **kw):
    subprocess.run([f"./{p}", str(sr), str(n), "out=y.f64"] + [f"{k}={v}" for k, v in kw.items()], check=True, capture_output=True)
    return np.fromfile("y.f64")

rng = np.random.default_rng(1)
np.asarray(rng.uniform(-1, 1, 5 * 96000), dtype=np.float32).tofile("noise.f32")

for sr, M in ((48000, 1061), (96000, 2113)):
    print(f"SR {sr}, mt = 7.291 m, M = {M}")
    h = run("lib", sr, 4 * M, **{"in": "impulse"})
    taps = {int(i): float(h[i]) for i in np.nonzero(h)[0]}
    expect("impulse response is 1 at 0 and at M, 0 elsewhere", [] if taps == {0: 1.0, M: 1.0} else [f"taps {taps}"])
    a = run("lib", sr, 4 * sr, **{"in": "file:noise.f32"})
    b = run("orig", sr, 4 * sr, M=M, **{"in": "file:noise.f32"})
    d = float(np.max(np.abs(a - b)))
    expect("equals the original structure on noise", [] if d == 0 else [f"max |diff| {d:.2e}"])
    b1 = run("orig", sr, 4 * sr, M=M - 1, **{"in": "file:noise.f32"})
    d1 = float(np.max(np.abs(a - b1)))
    expect("MUTATION original at M-1", [] if d1 == 0 else [f"max |diff| {d1:.2e}"], should_fail=True)
    for k, name in ((2, "peak"), (1.5, "notch")):
        f = k * sr / M
        y = run("lib", sr, 4 * sr, **{"in": f"tone:{f}:1"})[2 * sr:]
        g = 20 * math.log10(max(math.sqrt(float(np.mean(y ** 2))) / math.sqrt(0.5), 1e-300))
        want = 20 * math.log10(max(abs(2 * math.cos(math.pi * f * M / sr)), 1e-300))
        print(f"    {name} at {f:.2f} Hz: {g:+.2f} dB (theory {want:+.2f})")
        if name == "peak":
            expect(f"peak gain +6.02 dB", [] if abs(g - 6.0206) < 0.01 else [f"{g:+.3f} dB"])
        else:
            expect(f"notch below -100 dB", [] if g < -100 else [f"{g:+.1f} dB"])
    y = run("lib", sr, 4 * sr, **{"in": "file:noise.f32"})
    x = np.fromfile("noise.f32", dtype=np.float32)[: 4 * sr].astype(float)
    print(f"    power on uniform white noise: {10 * math.log10(np.mean(y[M:] ** 2) / np.mean(x[M:] ** 2)):+.2f} dB (theory +3.01)")
print("CHECK", "OK" if ok else "FAILED")
