#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_hub_digest.h"

using Seam::strx::digest;
using Seam::strx::holdAction;
using Seam::strx::HoldAction;
using Seam::strx::GlideParams;
using Seam::strx::pinkTakeover;

static SeamHubMember member(uint32_t kind, uint32_t stone, bool active) {
    SeamHubMember m{};
    m.hasState = 1;
    m.state.kind = kind;
    m.state.stoneId = stone;
    m.state.active = active ? 1u : 0u;
    return m;
}

static SeamHubMember pink(uint32_t stone, bool active, int32_t slotStart) {
    SeamHubMember m = member(kSeamHubPink, stone, active);
    m.state.levelDb = -23.0;
    m.state.u.pink.slotStart = slotStart;
    m.state.u.pink.slotCount = 4;
    return m;
}

// Fixed defaults for every generation-parameter field the fingerprint reads,
// so tests that don't care about a specific field still get a fully and
// consistently populated record (matching what ltglide publishes).
// `levelDb` is the field GS's in-host finding hinges on, so it is the one
// callers override via the extra parameter.
static SeamHubMember glideLevel(uint32_t stone, bool active, uint64_t pass, double levelDb) {
    SeamHubMember m = member(kSeamHubGlide, stone, active);
    m.state.levelDb = levelDb;
    m.state.sampleRate = 48000.0;
    m.state.u.glide.passCounter = pass;
    m.state.u.glide.passStartSample = 12345;
    m.state.u.glide.f0 = 40.0;
    m.state.u.glide.f1 = 12000.0;
    m.state.u.glide.durationSec = 30.0;
    m.state.u.glide.deltaSec = 1.0;
    m.state.u.glide.sweepMode = 1;
    m.state.u.glide.diracMode = 0;
    return m;
}
static SeamHubMember glide(uint32_t stone, bool active, uint64_t pass) {
    return glideLevel(stone, active, pass, -20.0);
}
static SeamHubMember decoder(bool active) {
    SeamHubMember m = member(kSeamHubDecoder, 1, active);
    m.state.u.decoder.test = 1;
    return m;
}

TEST_CASE("digest of an unavailable bus reports nothing") {
    const Seam::strx::HubDigest d = digest(nullptr, 0, /*available*/false);
    CHECK_FALSE(d.available);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest with no records") {
    SeamHubMember recs[1];
    const Seam::strx::HubDigest d = digest(recs, 0, true);
    CHECK(d.available);
    CHECK(d.count == 0);
    CHECK(d.firstActive == -1);
    CHECK(d.idleCount == 0);
}

TEST_CASE("digest counts idle records and finds none sounding") {
    SeamHubMember recs[3] = { pink(1,false,0), pink(2,false,4), pink(3,false,8) };
    const Seam::strx::HubDigest d = digest(recs, 3, true);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK(d.idleCount == 3);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest names the first active record and counts the rest") {
    SeamHubMember recs[4] = { pink(1,false,0), pink(2,true,4), pink(3,true,8), pink(4,false,12) };
    const Seam::strx::HubDigest d = digest(recs, 4, true);
    CHECK(d.count == 4);            // user-visible: compose() reads "no emitter" off count == 0
    CHECK(d.firstActive == 1);      // index of STONE 2
    CHECK(d.activeCount == 2);      // the collision the status line must flag
    CHECK(d.idleCount == 2);
    CHECK_FALSE(d.glide);           // pink is sounding, not a sweep
}

TEST_CASE("digest of an unavailable bus reports nothing even with live records") {
    // The prior "unavailable" case only passes nullptr/n==0, so it can never
    // exercise the `!available` clause on its own (the `!recs` check already
    // returns early). Here the records are real and active; only `available`
    // is false, so the `!available` short-circuit is what has to fire.
    SeamHubMember recs[2] = { pink(1,true,0), glide(2,true,3) };
    const Seam::strx::HubDigest d = digest(recs, 2, /*available*/false);
    CHECK_FALSE(d.available);
    CHECK(d.count == 0);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest reports glide and its pass counter only when glide is the first active") {
    SeamHubMember recs[2] = { pink(1,false,0), glide(2,true,7) };
    const Seam::strx::HubDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 1);
    CHECK(d.glide);
    CHECK(d.passCounter == 7u);
}

TEST_CASE("an idle glide does not put the spectrum in glide mode") {
    // The spectrum follows the SOUNDING emitter. A registered-but-idle ltglide
    // must not switch the analysis away from the pink average.
    SeamHubMember recs[2] = { pink(1,true,0), glide(2,false,7) };
    const Seam::strx::HubDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest follows the same record the status line names") {
    // Both views read this one digest, so they cannot disagree about which
    // emitter is "the" emitter when the by-method rule is violated.
    SeamHubMember recs[2] = { glide(1,true,3), pink(2,true,0) };
    const Seam::strx::HubDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 0);
    CHECK(d.glide);
    CHECK(d.passCounter == 3u);
    CHECK(d.activeCount == 2);
}

//──────────────────────────────────────────────────────────────────────────
// holdAction (via its boolean edge, "!= HoldAction::None") — the new-pass
// decision CalbusWatch::poll() drives the hold epoch with. Exercised through
// digest() + real record builders (not hand-built HubDigest values) so
// the tests cover the actual seam between the two functions, the same seam
// that hid the cross-instance collision. These five cases used to pin
// shouldResetHold(), a boolean-only wrapper around holdAction() that kept
// its own GlideParams in a function-local static; that wrapper had zero
// production callers and its scenarios are fully subsumed by holdAction(),
// so it was deleted and these migrated to call holdAction() directly.
//──────────────────────────────────────────────────────────────────────────

TEST_CASE("holdAction bumps once on a new pass, not again for the same counter") {
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember recs[1] = { glide(1, true, 1) };   // instance's first pass
    const auto d = digest(recs, 1, true);
    CHECK(holdAction(d, lastPass, lastParams) != HoldAction::None);  // new pass -> bump
    CHECK(lastPass == 1u);
    CHECK(holdAction(d, lastPass, lastParams) == HoldAction::None);  // re-polled, same pass -> no bump
    CHECK(holdAction(d, lastPass, lastParams) == HoldAction::None);  // polled again -> still no bump
}

TEST_CASE("holdAction catches the cross-instance collision GS's workflow hits") {
    // Calibrate STONE 1 (one pass, passCounter == 1), stop it, start STONE 2's
    // ltglide (a DIFFERENT instance whose first pass is ALSO passCounter == 1).
    // Without the per-emitter sentinel reset, "1 != lastPass" would be false
    // and STONE 2's pass would never clear STONE 1's max-hold curve.
    uint64_t lastPass = 0;
    GlideParams lastParams;

    SeamHubMember stoneOnePass1[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(stoneOnePass1, 1, true), lastPass, lastParams) != HoldAction::None);
    CHECK(lastPass == 1u);

    // STONE 1 stops: no active glide anywhere on the bus (unregistered, or
    // simply idle) -- the gap the fix relies on existing between instances.
    SeamHubMember noneActive[1] = { glide(1, false, 1) };
    CHECK(holdAction(digest(noneActive, 1, true), lastPass, lastParams) == HoldAction::None);
    CHECK(lastPass == 0u);                              // sentinel cleared

    // STONE 2 starts: a DIFFERENT instance, its own first pass, also == 1.
    SeamHubMember stoneTwoPass1[1] = { glide(2, true, 1) };
    CHECK(holdAction(digest(stoneTwoPass1, 1, true), lastPass, lastParams) != HoldAction::None);  // MUST bump
    CHECK(lastPass == 1u);
}

TEST_CASE("holdAction never bumps for a registered-but-idle glide") {
    // Prime lastPass from a REAL prior pass first (rather than starting from
    // the 0 default) so this test cannot pass merely because an idle glide's
    // digest.passCounter also happens to default to 0 -- it must exercise the
    // `!d.glide` gate itself, not a coincidence of two zeros matching.
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember activePass[1] = { glide(1, true, 5) };
    CHECK(holdAction(digest(activePass, 1, true), lastPass, lastParams) != HoldAction::None);
    CHECK(lastPass == 5u);

    SeamHubMember recs[1] = { glide(1, false, 5) };  // idle: passCounter carried over, not sounding
    const auto d = digest(recs, 1, true);
    CHECK_FALSE(d.glide);
    CHECK(holdAction(d, lastPass, lastParams) == HoldAction::None);
    CHECK(holdAction(d, lastPass, lastParams) == HoldAction::None);  // polled again, still idle -> still no bump
}

TEST_CASE("holdAction never bumps for a pink emitter") {
    // Same priming as the idle-glide case above, and for the same reason:
    // pink's digest.passCounter also defaults to 0, so this must start from a
    // nonzero lastPass to actually exercise the `!d.glide` gate.
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember activePass[1] = { glide(1, true, 5) };
    CHECK(holdAction(digest(activePass, 1, true), lastPass, lastParams) != HoldAction::None);
    CHECK(lastPass == 5u);

    SeamHubMember recs[1] = { pink(1, true, 0) };
    const auto d = digest(recs, 1, true);
    CHECK_FALSE(d.glide);
    CHECK(holdAction(d, lastPass, lastParams) == HoldAction::None);
    CHECK(lastPass == 0u);
}

TEST_CASE("holdAction bumps once per pass in a loop, not once per poll") {
    // A looping instance never goes idle between passes (running() stays true
    // across the loop), so the sentinel is never cleared by the !glide branch
    // -- but passCounter itself increments on every new pass, so the != check
    // alone must keep catching each one, and must NOT re-fire on repeated
    // polls of the same pass.
    uint64_t lastPass = 0;
    GlideParams lastParams;

    SeamHubMember pass1[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(pass1, 1, true), lastPass, lastParams) != HoldAction::None);
    CHECK(holdAction(digest(pass1, 1, true), lastPass, lastParams) == HoldAction::None);  // GUI polls faster than the pass changes
    CHECK(holdAction(digest(pass1, 1, true), lastPass, lastParams) == HoldAction::None);

    SeamHubMember pass2[1] = { glide(1, true, 2) };   // loop advanced to pass 2, still active
    CHECK(holdAction(digest(pass2, 1, true), lastPass, lastParams) != HoldAction::None);
    CHECK(holdAction(digest(pass2, 1, true), lastPass, lastParams) == HoldAction::None);

    SeamHubMember pass3[1] = { glide(1, true, 3) };
    CHECK(holdAction(digest(pass3, 1, true), lastPass, lastParams) != HoldAction::None);
}

//──────────────────────────────────────────────────────────────────────────
// holdAction — the three-outcome refinement of shouldResetHold that the
// cross-loop accumulator needs: a SessionStart clears the accumulation, a
// PassBoundary folds the completed pass into it. Same seam as above:
// exercised through digest() + real record builders.
//──────────────────────────────────────────────────────────────────────────

TEST_CASE("holdAction: the first pass after idle is a session start") {
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember recs[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(recs, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    CHECK(lastPass == 1u);
    CHECK(holdAction(digest(recs, 1, true), lastPass, lastParams) == HoldAction::None);  // same pass re-polled
}

TEST_CASE("holdAction: loop passes after the first are pass boundaries") {
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember pass1[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(pass1, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    SeamHubMember pass2[1] = { glide(1, true, 2) };   // loop advanced, still active
    CHECK(holdAction(digest(pass2, 1, true), lastPass, lastParams) == HoldAction::PassBoundary);
    CHECK(holdAction(digest(pass2, 1, true), lastPass, lastParams) == HoldAction::None);
    SeamHubMember pass3[1] = { glide(1, true, 3) };
    CHECK(holdAction(digest(pass3, 1, true), lastPass, lastParams) == HoldAction::PassBoundary);
}

TEST_CASE("holdAction: a different STONE after a gap starts a NEW session, never a boundary") {
    // Same scenario as the cross-instance collision above: fold STONE 2's
    // first pass into STONE 1's accumulation and the whole measurement lies.
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember stoneOne[1] = { glide(1, true, 1) };
    CHECK(holdAction(digest(stoneOne, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    SeamHubMember gap[1] = { glide(1, false, 1) };     // nothing sounding
    CHECK(holdAction(digest(gap, 1, true), lastPass, lastParams) == HoldAction::None);
    CHECK(lastPass == 0u);                                // sentinel cleared
    SeamHubMember stoneTwo[1] = { glide(2, true, 1) }; // different instance, its own pass 1
    CHECK(holdAction(digest(stoneTwo, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
}

TEST_CASE("holdAction: pink or idle emitters never act and clear the sentinel") {
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember activePass[1] = { glide(1, true, 5) };
    CHECK(holdAction(digest(activePass, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    CHECK(lastPass == 5u);
    SeamHubMember pinkRec[1] = { pink(1, true, 0) };
    CHECK(holdAction(digest(pinkRec, 1, true), lastPass, lastParams) == HoldAction::None);
    CHECK(lastPass == 0u);
}

TEST_CASE("holdAction: a level change mid-loop starts a new session") {
    // GS's in-host finding: changing ltglide's Level during LOOP must not be
    // folded into the accumulation as if it were just another pass at the
    // same stimulus -- it invalidates the whole session.
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember pass1[1] = { glideLevel(1, true, 1, -20.0) };
    CHECK(holdAction(digest(pass1, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    SeamHubMember pass2[1] = { glideLevel(1, true, 2, -20.0) };  // same params, loop advanced
    CHECK(holdAction(digest(pass2, 1, true), lastPass, lastParams) == HoldAction::PassBoundary);
    SeamHubMember pass3[1] = { glideLevel(1, true, 3, -14.0) };  // Level changed mid-loop
    CHECK(holdAction(digest(pass3, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    SeamHubMember pass4[1] = { glideLevel(1, true, 4, -14.0) };  // new params, loop advanced again
    CHECK(holdAction(digest(pass4, 1, true), lastPass, lastParams) == HoldAction::PassBoundary);
}

TEST_CASE("holdAction: a param change with no pass advance still starts a new session") {
    // Covers the run-edge republish case: the same passCounter is re-polled
    // (e.g. the record is republished for another reason) but a generation
    // parameter differs from what was last seen -- the passCounter-only
    // check would report None and the stale accumulation would survive.
    uint64_t lastPass = 0;
    GlideParams lastParams;
    SeamHubMember pass1[1] = { glideLevel(1, true, 3, -20.0) };
    CHECK(holdAction(digest(pass1, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
    SeamHubMember samePassNewLevel[1] = { glideLevel(1, true, 3, -14.0) };
    CHECK(holdAction(digest(samePassNewLevel, 1, true), lastPass, lastParams) == HoldAction::SessionStart);
}

TEST_CASE("digest captures the active glide's generation parameters") {
    SeamHubMember recs[1] = { glideLevel(3, true, 2, -14.0) };
    const auto d = digest(recs, 1, true);
    CHECK(d.glide);
    CHECK(d.params.stoneId == 3u);
    CHECK(d.params.levelDb == doctest::Approx(-14.0));
    CHECK(d.params.sampleRate == doctest::Approx(48000.0));
    CHECK(d.params.f0 == doctest::Approx(40.0));
    CHECK(d.params.f1 == doctest::Approx(12000.0));
    CHECK(d.params.durationSec == doctest::Approx(30.0));
    CHECK(d.params.deltaSec == doctest::Approx(1.0));
    CHECK(d.params.sweepMode == 1u);
    CHECK(d.params.diracMode == 0u);
}

//──────────────────────────────────────────────────────────────────────────
// pinkTakeover — the edge detector that fires exactly once when a non-glide
// (pink) emitter becomes the active one, so CalbusWatch can bump the session
// epoch and permanently discard the glide accumulation the pink interrupted
// (GS's "last measure wins" decision, 2026-07-21): a stopped pink must never
// let a stale glide accumulation reappear on the spectrum.
//──────────────────────────────────────────────────────────────────────────

TEST_CASE("pinkTakeover fires once when pink becomes active, not on re-polls") {
    bool lastNonGlideActive = false;
    SeamHubMember recs[1] = { pink(1, true, 0) };
    const auto d = digest(recs, 1, true);
    CHECK(pinkTakeover(d, lastNonGlideActive));            // edge: pink just took over
    CHECK(lastNonGlideActive);
    CHECK_FALSE(pinkTakeover(d, lastNonGlideActive));      // re-polled, still pink -> no edge
    CHECK_FALSE(pinkTakeover(d, lastNonGlideActive));
}

TEST_CASE("pinkTakeover fires again after pink stops and a new pink takes over") {
    bool lastNonGlideActive = false;
    SeamHubMember takeover1[1] = { pink(1, true, 0) };
    CHECK(pinkTakeover(digest(takeover1, 1, true), lastNonGlideActive));

    SeamHubMember silence[1] = { pink(1, false, 0) };   // pink stops: nothing active
    CHECK_FALSE(pinkTakeover(digest(silence, 1, true), lastNonGlideActive));
    CHECK_FALSE(lastNonGlideActive);

    SeamHubMember takeover2[1] = { pink(2, true, 4) };  // a different pink takes over
    CHECK(pinkTakeover(digest(takeover2, 1, true), lastNonGlideActive));
}

TEST_CASE("pinkTakeover never fires while glide is the active emitter") {
    bool lastNonGlideActive = false;
    SeamHubMember recs[1] = { glide(1, true, 1) };
    CHECK_FALSE(pinkTakeover(digest(recs, 1, true), lastNonGlideActive));
    SeamHubMember pass2[1] = { glide(1, true, 2) };
    CHECK_FALSE(pinkTakeover(digest(pass2, 1, true), lastNonGlideActive));
}

TEST_CASE("pinkTakeover never fires when nothing is active") {
    bool lastNonGlideActive = false;
    SeamHubMember recs[1] = { pink(1, false, 0) };
    CHECK_FALSE(pinkTakeover(digest(recs, 1, true), lastNonGlideActive));
}

TEST_CASE("pinkTakeover never fires on an unavailable bus") {
    bool lastNonGlideActive = false;
    SeamHubMember recs[1] = { pink(1, true, 0) };
    CHECK_FALSE(pinkTakeover(digest(recs, 1, /*available*/false), lastNonGlideActive));
}

TEST_CASE("a decoder state is not an emitter") {
    SeamHubMember recs[2] = { decoder(true), pink(1, false, 0) };
    const auto d = digest(recs, 2, true);
    CHECK(d.available);
    CHECK(d.count == 1);          // only the pink counts
    CHECK(d.firstActive == -1);   // the active decoder is not sounding
    CHECK(d.activeCount == 0);
    CHECK(d.idleCount == 1);
    CHECK_FALSE(d.glide);
}

TEST_CASE("a member without state is ignored") {
    SeamHubMember recs[2] = { pink(1, true, 0), glide(2, true, 3) };
    recs[0].hasState = 0;
    const auto d = digest(recs, 2, true);
    CHECK(d.firstActive == 1);
    CHECK(d.activeCount == 1);
    CHECK(d.glide);
}
