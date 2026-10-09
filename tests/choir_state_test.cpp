#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "choir_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace choir;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the state round-trips POWER and output") {
    ParamBox a; a.store(Param::Power, 1.0); a.store(Param::Output, 0.8);
    MemoryStream stream; writeState(&stream, a); rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == kNumParams);
    CHECK(b.normalized(Param::Power) == 1.0);
    CHECK(b.normalized(Param::Output) == 0.8);
}

TEST_CASE("a short blob keeps the default output") {
    MemoryStream stream;
    { Steinberg::IBStreamer w(&stream, kLittleEndian); w.writeDouble(1.0); }
    rewindStream(stream);
    ParamBox b; b.store(Param::Output, 0.9);
    CHECK(readState(&stream, b) == 1);
    CHECK(b.plain().power);
    CHECK(b.normalized(Param::Output) == defaultNormalized(Param::Output));
}

TEST_CASE("the on-disk order is Power, Output, 1/f, Q, release, and nothing else") {
    ParamBox a; a.store(Param::Power, 1.0); a.store(Param::Output, 0.75);
    a.store(Param::Pink, 1.0); a.store(Param::Q, 0.25); a.store(Param::Release, 0.125);
    MemoryStream stream; writeState(&stream, a);
    Steinberg::int64 end = 0; stream.tell(&end);
    CHECK(end == 5 * (Steinberg::int64)sizeof(double));      // RESET is not state
    rewindStream(stream);
    Steinberg::IBStreamer r(&stream, kLittleEndian);
    double p = 0, o = 0, k = 0, q = 0, rel = 0;
    r.readDouble(p); r.readDouble(o); r.readDouble(k); r.readDouble(q); r.readDouble(rel);
    CHECK(p == 1.0); CHECK(o == 0.75); CHECK(k == 1.0); CHECK(q == 0.25); CHECK(rel == 0.125);
}

TEST_CASE("a session saved before 1/f, Q and release recalls the score's values") {
    MemoryStream stream;
    { Steinberg::IBStreamer w(&stream, kLittleEndian); w.writeDouble(1.0); w.writeDouble(0.6); }
    rewindStream(stream);
    ParamBox b; b.store(Param::Pink, 1.0); b.store(Param::Q, 0.0); b.store(Param::Release, 0.0);
    CHECK(readState(&stream, b) == 2);
    CHECK(b.plain().output == 0.6);
    CHECK_FALSE(b.plain().pink);
    CHECK(b.plain().q == doctest::Approx(350.0));
    CHECK(b.plain().release == doctest::Approx(1.5));
}
