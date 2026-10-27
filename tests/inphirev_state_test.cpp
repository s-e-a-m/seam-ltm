#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "inphirev_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace inphirev;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the processor's state round-trips every parameter") {
    ParamBox a;
    a.store(Param::Power, 1.0);
    a.store(Param::StoneId, stoneIdToNormalized(5));
    a.store(Param::Grid, gridToNormalized(3));
    a.store(Param::Sections, sectionsToNormalized(11));
    MemoryStream stream;
    writeState(&stream, a);
    rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == kStateFields);
    for (int i = 0; i < kNumParams; ++i) CHECK(b.normalized((Param)i) == a.normalized((Param)i));
    const Plain p = b.plain();
    CHECK(p.power);
    CHECK(p.stoneId == 5);
    CHECK(p.grid == 3);
    CHECK(p.sections == 11);
}

TEST_CASE("a short blob keeps the defaults for the fields it lacks") {
    MemoryStream stream;
    {
        Steinberg::IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(kStateVersion);
        w.writeDouble(1.0);                          // Power only
    }
    rewindStream(stream);
    ParamBox b;
    b.store(Param::Grid, gridToNormalized(2));      // a value the recall must replace by the default
    CHECK(readState(&stream, b) == 2);
    const Plain p = b.plain();
    CHECK(p.power);
    CHECK(p.grid == kDefaultGrid);
    CHECK(p.sections == kDefaultSections);
    CHECK(p.stoneId == 0);
}

TEST_CASE("recalled out-of-range values clamp (id 9, j 20, sections 0 and 40)") {
    for (int pass = 0; pass < 2; ++pass) {
        MemoryStream stream;
        {
            Steinberg::IBStreamer w(&stream, kLittleEndian);
            w.writeDouble(kStateVersion);
            w.writeDouble(0.0);
            w.writeDouble(stoneIdToNormalized(9));
            w.writeDouble(gridToNormalized(20));
            w.writeDouble(sectionsToNormalized(pass == 0 ? 0 : 40));
        }
        rewindStream(stream);
        ParamBox b;
        CHECK(readState(&stream, b) == kStateFields);
        const Plain p = b.plain();
        CHECK(p.stoneId == kMaxStoneId);
        CHECK(p.grid == kGridMax);
        CHECK(p.sections == (pass == 0 ? 1 : kMaxSections));
    }
}
