#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_calbus_digest.h"

using Seam::strx::digest;
using Seam::strx::shouldResetHold;

static SeamCalbusRecord pink(uint32_t stone, bool active, int32_t slotStart) {
    SeamCalbusRecord r{};
    r.kind = kSeamCalbusPink;
    r.stoneId = stone;
    r.active = active ? 1u : 0u;
    r.levelDb = -23.0;
    r.u.pink.slotStart = slotStart;
    r.u.pink.slotCount = 4;
    return r;
}
static SeamCalbusRecord glide(uint32_t stone, bool active, uint64_t pass) {
    SeamCalbusRecord r{};
    r.kind = kSeamCalbusGlide;
    r.stoneId = stone;
    r.active = active ? 1u : 0u;
    r.levelDb = -20.0;
    r.u.glide.passCounter = pass;
    r.u.glide.passStartSample = 12345;
    return r;
}

TEST_CASE("digest of an unavailable bus reports nothing") {
    const Seam::strx::CalbusDigest d = digest(nullptr, 0, /*available*/false);
    CHECK_FALSE(d.available);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest with no records") {
    SeamCalbusRecord recs[1];
    const Seam::strx::CalbusDigest d = digest(recs, 0, true);
    CHECK(d.available);
    CHECK(d.count == 0);
    CHECK(d.firstActive == -1);
    CHECK(d.idleCount == 0);
}

TEST_CASE("digest counts idle records and finds none sounding") {
    SeamCalbusRecord recs[3] = { pink(1,false,0), pink(2,false,4), pink(3,false,8) };
    const Seam::strx::CalbusDigest d = digest(recs, 3, true);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK(d.idleCount == 3);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest names the first active record and counts the rest") {
    SeamCalbusRecord recs[4] = { pink(1,false,0), pink(2,true,4), pink(3,true,8), pink(4,false,12) };
    const Seam::strx::CalbusDigest d = digest(recs, 4, true);
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
    SeamCalbusRecord recs[2] = { pink(1,true,0), glide(2,true,3) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, /*available*/false);
    CHECK_FALSE(d.available);
    CHECK(d.count == 0);
    CHECK(d.firstActive == -1);
    CHECK(d.activeCount == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest reports glide and its pass counter only when glide is the first active") {
    SeamCalbusRecord recs[2] = { pink(1,false,0), glide(2,true,7) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 1);
    CHECK(d.glide);
    CHECK(d.passCounter == 7u);
}

TEST_CASE("an idle glide does not put the spectrum in glide mode") {
    // The spectrum follows the SOUNDING emitter. A registered-but-idle ltglide
    // must not switch the analysis away from the pink average.
    SeamCalbusRecord recs[2] = { pink(1,true,0), glide(2,false,7) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 0);
    CHECK_FALSE(d.glide);
}

TEST_CASE("digest follows the same record the status line names") {
    // Both views read this one digest, so they cannot disagree about which
    // emitter is "the" emitter when the by-method rule is violated.
    SeamCalbusRecord recs[2] = { glide(1,true,3), pink(2,true,0) };
    const Seam::strx::CalbusDigest d = digest(recs, 2, true);
    CHECK(d.firstActive == 0);
    CHECK(d.glide);
    CHECK(d.passCounter == 3u);
    CHECK(d.activeCount == 2);
}

//──────────────────────────────────────────────────────────────────────────
// shouldResetHold — the new-pass decision CalbusWatch::poll() drives the
// hold epoch with. Exercised through digest() + real record builders (not
// hand-built CalbusDigest values) so the tests cover the actual seam between
// the two functions, the same seam that hid the cross-instance collision.
//──────────────────────────────────────────────────────────────────────────

TEST_CASE("shouldResetHold bumps once on a new pass, not again for the same counter") {
    uint64_t lastPass = 0;
    SeamCalbusRecord recs[1] = { glide(1, true, 1) };   // instance's first pass
    const auto d = digest(recs, 1, true);
    CHECK(shouldResetHold(d, lastPass));                // new pass -> bump
    CHECK(lastPass == 1u);
    CHECK_FALSE(shouldResetHold(d, lastPass));           // re-polled, same pass -> no bump
    CHECK_FALSE(shouldResetHold(d, lastPass));           // polled again -> still no bump
}

TEST_CASE("shouldResetHold catches the cross-instance collision GS's workflow hits") {
    // Calibrate STONE 1 (one pass, passCounter == 1), stop it, start STONE 2's
    // ltglide (a DIFFERENT instance whose first pass is ALSO passCounter == 1).
    // Without the per-emitter sentinel reset, "1 != lastPass" would be false
    // and STONE 2's pass would never clear STONE 1's max-hold curve.
    uint64_t lastPass = 0;

    SeamCalbusRecord stoneOnePass1[1] = { glide(1, true, 1) };
    CHECK(shouldResetHold(digest(stoneOnePass1, 1, true), lastPass));
    CHECK(lastPass == 1u);

    // STONE 1 stops: no active glide anywhere on the bus (unregistered, or
    // simply idle) -- the gap the fix relies on existing between instances.
    SeamCalbusRecord noneActive[1] = { glide(1, false, 1) };
    CHECK_FALSE(shouldResetHold(digest(noneActive, 1, true), lastPass));
    CHECK(lastPass == 0u);                              // sentinel cleared

    // STONE 2 starts: a DIFFERENT instance, its own first pass, also == 1.
    SeamCalbusRecord stoneTwoPass1[1] = { glide(2, true, 1) };
    CHECK(shouldResetHold(digest(stoneTwoPass1, 1, true), lastPass));  // MUST bump
    CHECK(lastPass == 1u);
}

TEST_CASE("shouldResetHold never bumps for a registered-but-idle glide") {
    // Prime lastPass from a REAL prior pass first (rather than starting from
    // the 0 default) so this test cannot pass merely because an idle glide's
    // digest.passCounter also happens to default to 0 -- it must exercise the
    // `!d.glide` gate itself, not a coincidence of two zeros matching.
    uint64_t lastPass = 0;
    SeamCalbusRecord activePass[1] = { glide(1, true, 5) };
    CHECK(shouldResetHold(digest(activePass, 1, true), lastPass));
    CHECK(lastPass == 5u);

    SeamCalbusRecord recs[1] = { glide(1, false, 5) };  // idle: passCounter carried over, not sounding
    const auto d = digest(recs, 1, true);
    CHECK_FALSE(d.glide);
    CHECK_FALSE(shouldResetHold(d, lastPass));
    CHECK_FALSE(shouldResetHold(d, lastPass));          // polled again, still idle -> still no bump
}

TEST_CASE("shouldResetHold never bumps for a pink emitter") {
    // Same priming as the idle-glide case above, and for the same reason:
    // pink's digest.passCounter also defaults to 0, so this must start from a
    // nonzero lastPass to actually exercise the `!d.glide` gate.
    uint64_t lastPass = 0;
    SeamCalbusRecord activePass[1] = { glide(1, true, 5) };
    CHECK(shouldResetHold(digest(activePass, 1, true), lastPass));
    CHECK(lastPass == 5u);

    SeamCalbusRecord recs[1] = { pink(1, true, 0) };
    const auto d = digest(recs, 1, true);
    CHECK_FALSE(d.glide);
    CHECK_FALSE(shouldResetHold(d, lastPass));
    CHECK(lastPass == 0u);
}

TEST_CASE("shouldResetHold bumps once per pass in a loop, not once per poll") {
    // A looping instance never goes idle between passes (running() stays true
    // across the loop), so the sentinel is never cleared by the !glide branch
    // -- but passCounter itself increments on every new pass, so the != check
    // alone must keep catching each one, and must NOT re-fire on repeated
    // polls of the same pass.
    uint64_t lastPass = 0;

    SeamCalbusRecord pass1[1] = { glide(1, true, 1) };
    CHECK(shouldResetHold(digest(pass1, 1, true), lastPass));
    CHECK_FALSE(shouldResetHold(digest(pass1, 1, true), lastPass));  // GUI polls faster than the pass changes
    CHECK_FALSE(shouldResetHold(digest(pass1, 1, true), lastPass));

    SeamCalbusRecord pass2[1] = { glide(1, true, 2) };   // loop advanced to pass 2, still active
    CHECK(shouldResetHold(digest(pass2, 1, true), lastPass));
    CHECK_FALSE(shouldResetHold(digest(pass2, 1, true), lastPass));

    SeamCalbusRecord pass3[1] = { glide(1, true, 3) };
    CHECK(shouldResetHold(digest(pass3, 1, true), lastPass));
}
