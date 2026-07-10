//===================================================================
// env_oa -- overlap-add Hann envelope follower  (ondemand PROTOTYPE)
//===================================================================
// STATUS: WIP SKELETON, sketched 2026-07-10. NOT in seam.pdclone.lib yet.
//
// WHY: spd.env is a sliding FIR -- sum(i, npoints, w(i)*(x@i)^2) -- that unrolls
// to `npoints` taps and takes >2 min to compile at env(2048,1024). Pd itself does
// NOT do that: env_tilde_perform is OVERLAP-ADD (~L active Hann windows, O(L)/sample).
// This prototype mirrors that structure so it compiles at the full window.
//
// RESULTS so far (scratch A/B vs the verified spd.env, small window 64/32):
//   * env_oa(2048,1024) compiles in ~0.16 s (vs >2 min for the FIR), on STABLE faust.
//   * DC=0.5 -> 93.979 dB, exactly matches spd.env (= powtodb(0.25)).
//   * 1 kHz sine -> matches spd.env within max 0.037 dB / mean 0.004 dB.
//
// REMAINING TUNING (next session, formal A/B vs oracle.py + Pd):
//   * the ~0.04 dB residual is window PHASE ALIGNMENT: the overlap-add lane sweeps
//     the Hann phase in the opposite orientation from the sliding FIR (a ~1-sample
//     shift; Hann is symmetric so it nearly cancels) and the hop/tick phase may be
//     one sample off from spd.env's ba.pulse(period). Reconcile the lane phase seed
//     and the capture instant, then re-verify to float precision.
//   * requires npoints % period == 0 (integer overlap L); Pd rounds realperiod to a
//     block multiple -- decide the convention.
//   * ba.time-free bounded counter used (good), but confirm behaviour across the
//     first npoints samples (warm-up) matches spd.env's zero-fill.
//
// ONDEMAND: the sAndH form below is stable faust. The ondemand form (commented)
// computes powtodb only at the hop, mapping 1:1 to `if (onset)` in the C++ port
// (ltburst idiom). For env, ondemand is optional (the accumulation is full-rate
// regardless); its real value here is exercising the overlap-add clock in the IDE.
//===================================================================
import("seam.lib");

env_oa(npoints, period, x) = level
with {
    L  = int(npoints / period);              // overlap factor (LAR: 2048/1024 = 2)
    x2 = x * x;

    // one shared bounded sample counter 0..npoints-1 (increments by 1/sample)...
    tick = step ~ _ with { step(c) = (c + 1) % npoints; };
    // ...and a per-lane phase = a one-time offset of that counter.
    phase(off) = (tick + off) % npoints;

    // Hann weight at a phase position (Pd's literal 3.14159, matching spd.env).
    hann(p) = (1.0 - cos((2.0 * 3.14159 * p) / npoints)) / npoints;

    // one overlap lane: running Hann-weighted sum of x2 over its window;
    // resets when its phase wraps, capturing the just-completed window sum.
    laneSum(off) = ba.sAndH(wrap, acc')
    with {
        p    = phase(off);
        wrap = p < p';                        // 1 at the wrap sample
        w    = hann(p);
        acc  = step ~ _;                      // reset-at-wrap accumulator
        step(a) = select2(wrap, a + w*x2, w*x2);
    };
    laneWrap(off) = p < p' with { p = phase(off); };

    // combine the L staggered lanes: exactly one wraps per `period`;
    // grab that lane's freshest completed window sum.
    anyWrap  = par(l, L, laneWrap(l*period)) :> _ > 0;
    freshest = par(l, L, laneSum(l*period) * laneWrap(l*period)) :> _;
    result   = ba.sAndH(anyWrap, freshest);

    // control-rate output -- stable-faust form:
    level = powtodb(result);
    // ondemand form (faust-od), compute powtodb only at the hop:
    // level = (anyWrap, result) : ondemand(powtodb);

    powtodb(pw) = select2(pw <= 0.0, max(0.0, 100.0 + 10.0*log10(pw)), 0.0);
};

process = env_oa(2048, 1024);   // the claim: compiles instantly at the full window
// process = env_oa(64, 32);    // small window for A/B vs spd.env(64,32)
