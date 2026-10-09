#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "abmodulex_state.h"
#include "public.sdk/source/common/memorystream.h"
#include <cmath>

using namespace Seam::abmodulex;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("a session saved before the menu (an empty state) opens on the upright tetrahedron") {
    MemoryStream stream;                    // getState wrote nothing before 2026-10-09
    ParamBox b;
    b.store(1.0);                           // the recall must replace it by the default
    CHECK(readState(&stream, b) == 0);
    CHECK_FALSE(b.dual());
}

TEST_CASE("the dual tetrahedron round-trips through the state") {
    ParamBox a; a.store(1.0);
    MemoryStream stream;
    writeState(&stream, a);
    rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == 1);
    CHECK(b.dual());
    CHECK(b.normalized() == 1.0);
}

TEST_CASE("the menu's second entry is the dual from 0.5 up; NaN stays upright") {
    ParamBox b;
    CHECK_FALSE(b.dual());                  // the default
    b.store(0.49); CHECK_FALSE(b.dual());
    b.store(0.5);  CHECK(b.dual());
    b.store(std::nan("")); CHECK_FALSE(b.dual());
}
