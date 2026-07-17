#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "strx_calbus_digest.h"

using Seam::strx::digest;

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
    CHECK(d.firstActive == 1);      // index of STONE 2
    CHECK(d.activeCount == 2);      // the collision the status line must flag
    CHECK(d.idleCount == 2);
    CHECK_FALSE(d.glide);           // pink is sounding, not a sweep
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
