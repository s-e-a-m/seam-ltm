//===================================================================
// env_oa -- overlap-add Hann envelope follower  (ondemand path)
//===================================================================
// STATUS: PROMOTED, 2026-07-10. This overlap-add is now the body of spd.env in
// seam.pdclone.lib (faust-libraries@dslar 50c4902); this file is kept as the
// standalone derivation + A/B harness. Float-exact vs the old FIR / the Pd oracle.
//
// WHY: spd.env is a sliding FIR -- sum(i, np, w(i)*(x@i)^2) -- that unrolls to `np`
// taps and takes >2 min to compile at env(2048,1024). Pd itself does NOT do that:
// env_tilde_perform is OVERLAP-ADD (~L active Hann windows, O(L)/sample). This
// mirrors that structure, so it compiles at the full window.
//
// VERIFICATION (scratch harness A/B, small window 64/32):
//   * env_oa(2048,1024) compiles in ~0.16 s (vs >2 min FIR), on STABLE faust.
//   * vs spd.env(64,32): max 7.6e-6 dB (DC exact 93.979; 1 kHz sine).
//   * vs the Pd oracle (env~ formula) over all 400 samples incl. warm-up: max 4.2e-6 dB.
//   -> matches to float32 precision; the residual is summation-order rounding only.
//
// HOW IT ALIGNS (the fix over the first sketch): each lane runs a DOWN phase d that
// reaches 0 exactly at the capture instant n = n_cap (n_cap ≡ off mod np, so the L
// lanes' captures union to n ≡ 0 mod pd, matching spd.env's ba.pulse(pd)). At d==0
// the weight hann(k) has landed on x(n_cap - k) for k = 0..np-1 -- identical to the
// FIR's sum(i, np, hann(i)*x(n-i)^2). No np-tap unroll: O(L)/sample.
//
// CONSTRAINT: np % pd == 0 (integer overlap L = np/pd). Pd rounds realperiod to a
// block multiple; here require exact divisibility (LAR: 2048/1024 = 2 -- fine).
//
// ONDEMAND: the sAndH form below is stable faust. The ondemand form (commented)
// computes powtodb only at the hop, mapping 1:1 to `if (onset)` in the C++ port
// (ltburst idiom). For env, ondemand is optional -- the accumulation is full-rate
// regardless; its value here is exercising the overlap-add clock in the IDE.
//===================================================================
import("seam.lib");

env_oa(np, pd, x) = level
with {
    L  = int(np / pd);                           // overlap factor (LAR: 2048/1024 = 2)
    x2 = x * x;
    wrapmod(a) = (a % np + np) % np;

    // shared bounded up-counter: up(n) = (n + 1) % np
    up = step ~ _ with { step(c) = (c + 1) % np; };

    // per-lane DOWN phase: d(off) == 0 exactly at n ≡ off (mod np), counting np-1..0.
    d(off)  = wrapmod(off + 1 - up);
    hann(p) = (1.0 - cos((2.0 * 3.14159 * p) / np)) / np;   // Pd's literal 3.14159

    // one overlap lane: Hann-weighted running sum of x2; reset at the start of each
    // window, so that at d==0 the accumulator holds sum_{k=0}^{np-1} hann(k)*x(n-k)^2.
    lane(off) = acc
    with {
        dd     = d(off);
        w      = hann(dd);
        newwin = dd == (np - 1);                 // first sample of a fresh window
        acc    = step ~ _ with { step(a) = select2(newwin, a + w*x2, w*x2); };
    };
    cap(off) = d(off) == 0;                       // capture instant for this lane

    // exactly one lane captures per hop; grab that lane's completed window sum.
    anyCap  = par(l, L, cap(l*pd)) :> _ > 0;
    grabbed = par(l, L, lane(l*pd) * cap(l*pd)) :> _;
    result  = ba.sAndH(anyCap, grabbed);

    // control-rate output -- stable-faust form:
    level = powtodb(result);
    // ondemand form (faust-od), compute powtodb only at the hop:
    // level = (anyCap, result) : ondemand(powtodb);

    powtodb(pw) = select2(pw <= 0.0, max(0.0, 100.0 + 10.0*log10(pw)), 0.0);
};

process = env_oa(2048, 1024);   // compiles instantly at the full window
// process = env_oa(64, 32);    // small window used for the A/B above
