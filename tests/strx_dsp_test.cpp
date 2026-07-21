#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_dsp.h"
#include <cmath>
#include <functional>
#include <vector>

using namespace Seam::strx;

// Drive the analyzer to steady state with a generated stereo signal.
static AnalysisFrame settle(std::function<void(int,float&,float&)> gen) {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(512), R(512);
    for (int blk = 0; blk < 400; ++blk) {              // ~4 s at 48k/512
        for (int i = 0; i < 512; ++i) gen(blk*512+i, L[i], R[i]);
        a.analyzeScalars(L.data(), R.data(), 512);
    }
    return a.frame();
}

TEST_CASE("mono (L=R): correlation ~1, width ~0, side at floor") {
    auto f = settle([](int n, float& l, float& r){
        l = r = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); });
    CHECK(f.correlation == doctest::Approx(1.0).epsilon(0.02));
    CHECK(f.width       == doctest::Approx(0.0).epsilon(0.02));
    CHECK(f.side        <= -55.0f);
    CHECK(f.panorama    == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("anti-phase (L=-R): correlation ~-1, width ~1, mid at floor") {
    auto f = settle([](int n, float& l, float& r){
        l = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); r = -l; });
    CHECK(f.correlation == doctest::Approx(-1.0).epsilon(0.02));
    CHECK(f.width       == doctest::Approx(1.0).epsilon(0.02));
    CHECK(f.mid         <= -55.0f);
}

TEST_CASE("left only: panorama fully left (~-1)") {
    auto f = settle([](int n, float& l, float& r){
        l = 0.5f*std::sin(2.0*M_PI*1000.0*n/48000.0); r = 0.0f; });
    CHECK(f.panorama == doctest::Approx(-1.0).epsilon(0.05));
}

TEST_CASE("goniometer: mono maps to the vertical axis (x~0, y!=0)") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    for (int i = 0; i < 1024; ++i)
        L[i] = R[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    a.analyze(L.data(), R.data(), 1024);
    const auto& f = a.frame();
    REQUIRE(f.numPoints > 0);
    float maxAbsX = 0, maxAbsY = 0;
    for (int i = 0; i < f.numPoints; ++i) {
        maxAbsX = std::max(maxAbsX, std::fabs(f.gx[i]));
        maxAbsY = std::max(maxAbsY, std::fabs(f.gy[i]));
    }
    CHECK(maxAbsX < 1e-3f);      // S = 0 on the horizontal axis
    CHECK(maxAbsY > 0.1f);       // M carries the energy (vertical)
}

TEST_CASE("spectrum: a 1 kHz mid tone peaks near the 1 kHz bin") {
    Analyzer a; a.prepare(48000.0);
    const int N = a.fftSize();
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i)
        L[i] = R[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    for (int b = 0; b < 8; ++b) a.analyze(L.data(), R.data(), N);
    const auto& f = a.frame();
    const int expBin = int(std::lround(1000.0 * N / 48000.0));
    int peak = 1;
    for (int k = 2; k < f.numBins; ++k) if (f.specM[k] > f.specM[peak]) peak = k;
    CHECK(std::abs(peak - expBin) <= 2);
}

TEST_CASE("triple-buffer: read after process returns the latest frame") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    for (int i = 0; i < 1024; ++i) { L[i] = 0.5f; R[i] = 0.0f; }  // left only
    a.process(L.data(), R.data(), 1024);
    AnalysisFrame out;
    REQUIRE(a.tryReadFrame(out));
    CHECK(out.panorama == doctest::Approx(-1.0).epsilon(0.1));
    CHECK_FALSE(a.tryReadFrame(out));    // nothing new since last read
}

TEST_CASE("triple-buffer: publish/read cycles, coalescing, and no-new") {
    Analyzer a; a.prepare(48000.0);
    std::vector<float> L(1024), R(1024);
    auto fillLeftOnly = [&]{ for (int i = 0; i < 1024; ++i) { L[i] = 0.5f; R[i] = 0.0f; } };
    fillLeftOnly();
    AnalysisFrame out;

    // Several publish->read cycles: each read sees a fresh frame, then reports
    // "nothing new" until the next publish.
    for (int cycle = 0; cycle < 4; ++cycle) {
        a.process(L.data(), R.data(), 1024);
        REQUIRE(a.tryReadFrame(out));
        CHECK(out.panorama == doctest::Approx(-1.0).epsilon(0.1));  // left-only stays panorama ~ -1
        CHECK_FALSE(a.tryReadFrame(out));   // nothing new since this read
    }

    // Two back-to-back publishes then ONE read: returns the latest only, and
    // the immediately-following read reports nothing new (the intermediate
    // frame was coalesced, never observed).
    a.process(L.data(), R.data(), 1024);
    a.process(L.data(), R.data(), 1024);
    REQUIRE(a.tryReadFrame(out));
    CHECK(out.panorama == doctest::Approx(-1.0).epsilon(0.1));
    CHECK_FALSE(a.tryReadFrame(out));   // only the latest was delivered; nothing more pending
}

TEST_CASE("Analyzer publishes a max-hold that survives silence") {
    Seam::strx::Analyzer a;
    a.prepare(48000.0);

    // A loud burst, then silence. The live spectrum decays; the hold does not.
    std::vector<float> loud(8192), quiet(8192, 0.0f);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    a.analyze(loud.data(), loud.data(), int(loud.size()));
    const Seam::strx::AnalysisFrame& f1 = a.frame();
    int peakBin = 0;
    for (int k = 1; k < f1.numBins; ++k)
        if (f1.holdM[k] > f1.holdM[peakBin]) peakBin = k;
    const float peakHold = f1.holdM[peakBin];
    // Bounded strictly below 0 dBFS, not just above -40: AnalysisFrame
    // zero-initializes holdM (`= {}`), and a coherent-gain-calibrated dB value
    // for this half-amplitude tone can never legitimately be exactly 0.0f, so
    // an unbounded "> -40" alone would pass even if hM were never copied into
    // the frame (0.0f satisfies "> -40" trivially).
    CHECK(peakHold > -40.0f);
    CHECK(peakHold < 0.0f);

    for (int i = 0; i < 12; ++i) a.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    const Seam::strx::AnalysisFrame& f2 = a.frame();
    CHECK(f2.specM[peakBin] < peakHold - 6.0f);   // live decayed
    CHECK(f2.holdM[peakBin] == peakHold);         // hold held
}

TEST_CASE("Analyzer resetHold clears the published hold") {
    Seam::strx::Analyzer a;
    a.prepare(48000.0);
    std::vector<float> loud(8192);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    a.analyze(loud.data(), loud.data(), int(loud.size()));
    int peakBin = 0;
    for (int k = 1; k < a.frame().numBins; ++k)
        if (a.frame().holdM[k] > a.frame().holdM[peakBin]) peakBin = k;
    CHECK(a.frame().holdM[peakBin] > -40.0f);

    // Flush the Welch analysis window (kFftSize=4096, 50% overlap => a 2-hop
    // ring) of the loud burst BEFORE calling resetHold(). Welch::resetHold()
    // deliberately leaves the ring untouched (seam_fft.h), so if the ring
    // still holds loud samples, the very next post-reset frame straddles the
    // old/new boundary right where the Hann window's gain is highest, and
    // that leaked energy latches into the hold permanently (a max-hold never
    // decreases). One quiet 8192-sample block is two full kFftSize windows,
    // which guarantees the ring is completely silent by the time we reset.
    std::vector<float> quiet(8192, 0.0f);
    a.analyze(quiet.data(), quiet.data(), int(quiet.size()));

    a.resetHold();
    a.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    CHECK(a.frame().holdM[peakBin] == -120.0f);
}

TEST_CASE("Analyzer glide mode is idempotent and switches the live decay") {
    // Same excitation and same silence in the two modes: glide's fast tau must
    // decay further. This is the property the swept display depends on.
    std::vector<float> loud(8192), quiet(8192, 0.0f);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    Seam::strx::Analyzer pink, glide;
    pink.prepare(48000.0);
    glide.prepare(48000.0);
    CHECK_FALSE(pink.glideMode());
    glide.setGlideMode(true);
    glide.setGlideMode(true);          // idempotent: must not reset anything
    CHECK(glide.glideMode());

    pink.analyze(loud.data(), loud.data(), int(loud.size()));
    glide.analyze(loud.data(), loud.data(), int(loud.size()));
    int peakBin = 0;
    for (int k = 1; k < pink.frame().numBins; ++k)
        if (pink.frame().holdM[k] > pink.frame().holdM[peakBin]) peakBin = k;

    for (int i = 0; i < 4; ++i) {
        pink.analyze(quiet.data(), quiet.data(), int(quiet.size()));
        glide.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    }
    CHECK(glide.frame().specM[peakBin] < pink.frame().specM[peakBin] - 6.0f);

    glide.setGlideMode(false);
    CHECK_FALSE(glide.glideMode());
}

TEST_CASE("Analyzer prepare() honours a glide mode set before it") {
    // Mirrors "Analyzer glide mode is idempotent and switches the live decay"
    // above, but sets glide mode BEFORE calling prepare() rather than after.
    // This is the sample-rate-change-mid-glide scenario: the host calls
    // setupProcessing() -> prepare(fs) while a sweep is sounding, so glide_
    // is already true when prepare() runs. prepare() must derive its tau
    // from the current mode (not hardcode the pink tau), because
    // setGlideMode() is idempotent — a later setGlideMode(true) from the
    // processor would short-circuit on `on == glide_` and never repair a
    // wrong tau left behind by prepare().
    std::vector<float> loud(8192), quiet(8192, 0.0f);
    for (size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * double(i) / 48000.0);

    Seam::strx::Analyzer pink, glide;
    pink.prepare(48000.0);

    glide.setGlideMode(true);          // mode set BEFORE prepare()
    CHECK(glide.glideMode());
    glide.prepare(48000.0);            // must pick up glide tau, not reset to pink
    CHECK(glide.glideMode());          // prepare() must not disturb the mode either

    pink.analyze(loud.data(), loud.data(), int(loud.size()));
    glide.analyze(loud.data(), loud.data(), int(loud.size()));
    int peakBin = 0;
    for (int k = 1; k < pink.frame().numBins; ++k)
        if (pink.frame().holdM[k] > pink.frame().holdM[peakBin]) peakBin = k;

    for (int i = 0; i < 4; ++i) {
        pink.analyze(quiet.data(), quiet.data(), int(quiet.size()));
        glide.analyze(quiet.data(), quiet.data(), int(quiet.size()));
    }
    CHECK(glide.frame().specM[peakBin] < pink.frame().specM[peakBin] - 6.0f);
}

TEST_CASE("cross-pass MIN keeps what every pass contains, discards what one pass had") {
    Analyzer a; a.prepare(48000.0);
    a.setGlideMode(true);   // matches production: the accumulator only runs
                            // while a sweep is sounding, so strx is always in
                            // glide mode (fast tau, 0.1 s) here too.
    const int N = a.fftSize();
    std::vector<float> tone(2*N), mixed(2*N), quiet(2*N, 0.0f);
    for (int i = 0; i < 2*N; ++i) {
        tone[i]  = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
        mixed[i] = tone[i] + 0.5f*std::sin(2.0*M_PI*3000.0*i/48000.0);
    }
    // Per pass: feed the pass signal, then four full quiet windows to flush
    // the Welch ring and decay spectral leakage (resetHold leaves the ring
    // untouched — see the existing "Analyzer resetHold clears the published
    // hold" case; silence never raises a max-hold, so the pass curve is
    // unharmed), then fold.
    auto feedPass = [&](std::vector<float>& sig) {
        a.analyze(sig.data(), sig.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.completePass();
    };
    a.startSession();
    // Arm folding: the first boundary after a session start always discards
    // (observed-from-the-beginning rule — see completePass()), so feed
    // anything and consume that discard before the passes we count on.
    feedPass(quiet);
    feedPass(mixed);   // pass 1 (armed, folds): 1 kHz + an intermittent 3 kHz disturbance
    feedPass(tone);    // pass 2 (folds): clean 1 kHz — the disturbance is absent
    a.analyze(quiet.data(), quiet.data(), 2*N);   // publish acc into the frame

    const auto& f = a.frame();
    CHECK(f.accPasses == 2);
    const int bin1k = int(std::lround(1000.0 * N / 48000.0));
    const int bin3k = int(std::lround(3000.0 * N / 48000.0));
    // Present in EVERY pass -> survives the MIN. Bounded strictly below 0:
    // AnalysisFrame zero-initializes accM, so a never-copied 0.0f must fail.
    CHECK(f.accM[bin1k] > -40.0f);
    CHECK(f.accM[bin1k] < 0.0f);
    // Present in ONE pass only -> discarded by the MIN. Measured floor here
    // is Hann window spectral leakage from the 1 kHz tone into the 3 kHz bin
    // (~-55.4 dB down, ~2000 Hz / ~171 bins away) — NOT EMA carry-over: the
    // MIN folds welchM_.holdDb(), a per-frame raw-FFT-power max that never
    // touches the EMA, so setGlideMode() above (and its tau) has no effect
    // on this bound at all. -60 dB fails empirically; -50 dB is the loosest
    // 10-dB-step bound that passes.
    CHECK(f.accM[bin3k] < -50.0f);
    // Same L=R signal -> no side energy; also proves accS is copied (its
    // never-copied default 0.0f would fail this bound).
    CHECK(f.accS[bin1k] < -60.0f);
}

TEST_CASE("startSession clears the accumulator; acc changes only on completePass") {
    Analyzer a; a.prepare(48000.0);
    const int N = a.fftSize();
    std::vector<float> tone(2*N), quiet(2*N, 0.0f);
    for (int i = 0; i < 2*N; ++i) tone[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);
    const int bin1k = int(std::lround(1000.0 * N / 48000.0));

    // Arm folding: the boundary right after prepare()/reset() is itself a
    // "first boundary" and discards (observed-from-the-beginning rule).
    a.analyze(quiet.data(), quiet.data(), 2*N);
    a.completePass();
    CHECK(a.accPasses() == 0);   // discard, not a fold

    a.analyze(tone.data(), tone.data(), 2*N);
    a.analyze(quiet.data(), quiet.data(), 2*N);
    a.completePass();            // now armed: this one folds
    a.analyze(quiet.data(), quiet.data(), 2*N);   // publish
    CHECK(a.frame().accPasses == 1);
    CHECK(a.frame().accM[bin1k] > -40.0f);

    // More audio WITHOUT a boundary must not touch the accumulation — this
    // is the partial-pass guarantee: no completePass(), no fold.
    a.analyze(tone.data(), tone.data(), 2*N);
    CHECK(a.frame().accPasses == 1);

    a.startSession();
    a.analyze(quiet.data(), quiet.data(), 2*N);   // publish
    CHECK(a.frame().accPasses == 0);
    // Pin the clearing of the VALUES too, not just the counter: today
    // stale accM/accS survive under the first-fold-copy semantics unless
    // startSession() actually re-fills them to the floor.
    CHECK(a.frame().accM[bin1k] < -100.0f);
}

TEST_CASE("completePass discards the first boundary after a session start") {
    // GS's decided rule: the accumulator only folds a pass it observed from
    // its very beginning. A SessionStart can fire mid-pass (editor opened,
    // or processing reactivated, while ltglide is already looping), so the
    // hold at the NEXT boundary may be a partial pass.
    //
    // Proof strategy: run two analyzers through the identical real pass
    // (toneB), but give one of them a LOUD, unrelated tone (toneA) across
    // its pre-arm boundary and the other silence there instead. If the
    // discard genuinely contributes nothing, both must converge on an
    // IDENTICAL acc — this doesn't rely on assuming how far window leakage
    // reaches, unlike a fixed dB bound would (see the MIN test above, where
    // that leakage floor turned out to sit around -55 dB, not -60).
    Analyzer withPreArmTone, control;
    withPreArmTone.prepare(48000.0);
    control.prepare(48000.0);
    const int N = withPreArmTone.fftSize();
    std::vector<float> toneA(2*N), toneB(2*N), quiet(2*N, 0.0f);
    for (int i = 0; i < 2*N; ++i) {
        toneA[i] = 0.5f*std::sin(2.0*M_PI*3000.0*i/48000.0);  // pre-arm partial only
        toneB[i] = 0.5f*std::sin(2.0*M_PI*1000.0*i/48000.0);  // the armed, fully-observed pass
    }
    const int bin1k = int(std::lround(1000.0 * N / 48000.0));
    const int bin3k = int(std::lround(3000.0 * N / 48000.0));

    auto runSession = [&](Analyzer& a, std::vector<float>& preArmSignal) {
        a.startSession();
        // First boundary after the session start: discard, per the rule above.
        a.analyze(preArmSignal.data(), preArmSignal.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.completePass();
        CHECK(a.accPasses() == 0);   // discarded, not folded
        a.analyze(quiet.data(), quiet.data(), 2*N);   // publish; read the cleared hold
        CHECK(a.frame().holdM[bin3k] == -120.0f);     // the discard cleared the hold too

        // Second boundary: a real, fully-observed pass — this one folds.
        a.analyze(toneB.data(), toneB.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.analyze(quiet.data(), quiet.data(), 2*N);
        a.completePass();
        CHECK(a.accPasses() == 1);
        a.analyze(quiet.data(), quiet.data(), 2*N);   // publish
        CHECK(a.frame().accPasses == 1);
    };
    runSession(withPreArmTone, toneA);   // pre-arm boundary carries a loud 3 kHz tone
    runSession(control, quiet);          // pre-arm boundary carries silence instead

    // Both converge on the same acc after folding the identical real pass:
    // toneA never reached the accumulator, however loud it was.
    for (int k = 0; k < withPreArmTone.frame().numBins; ++k) {
        CHECK(withPreArmTone.frame().accM[k] == doctest::Approx(control.frame().accM[k]));
        CHECK(withPreArmTone.frame().accS[k] == doctest::Approx(control.frame().accS[k]));
    }
    CHECK(withPreArmTone.frame().accM[bin1k] > -40.0f);   // sanity: the real pass did fold
}
