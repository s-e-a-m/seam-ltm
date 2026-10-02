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

TEST_CASE("the on-disk order is Power, Output, and nothing else") {
    ParamBox a; a.store(Param::Power, 1.0); a.store(Param::Output, 0.75);
    MemoryStream stream; writeState(&stream, a);
    Steinberg::int64 end = 0; stream.tell(&end);
    CHECK(end == 2 * (Steinberg::int64)sizeof(double));      // RESET is not state
    rewindStream(stream);
    Steinberg::IBStreamer r(&stream, kLittleEndian);
    double p = 0, o = 0; r.readDouble(p); r.readDouble(o);
    CHECK(p == 1.0); CHECK(o == 0.75);
}
