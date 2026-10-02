#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "stunedrev_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace stunedrev;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the processor's state round-trips every parameter") {
    ParamBox a;
    a.store(Param::Power, 1.0);
    a.store(Param::T1, timeToNormalized(12));
    a.store(Param::T4, timeToNormalized(100));
    a.store(Param::Input, 0.3);
    a.store(Param::Output, 0.9);
    MemoryStream stream;
    writeState(&stream, a);
    rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == kNumParams);
    for (int i = 0; i < kNumParams; ++i) CHECK(b.normalized((Param)i) == a.normalized((Param)i));
}

TEST_CASE("a short blob keeps the defaults for the fields it lacks") {
    MemoryStream stream;
    {
        Steinberg::IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(1.0);                          // Power only
    }
    rewindStream(stream);
    ParamBox b;
    b.store(Param::T2, timeToNormalized(5));        // a value the recall must replace by the default
    CHECK(readState(&stream, b) == 1);
    CHECK(b.plain().power);
    CHECK(b.plain().t[1] == kDefaultTimes[1]);
}
