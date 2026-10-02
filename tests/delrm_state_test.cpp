#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "delrm_state.h"
#include "public.sdk/source/common/memorystream.h"

using namespace delrm;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the processor's state round-trips every parameter") {
    ParamBox a;
    a.store(Param::Power, 1.0);
    a.store(Param::Distance, distanceToNormalized(12.345));
    a.store(Param::Output, 0.8);
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
        w.writeDouble(1.0);                              // Power only
    }
    rewindStream(stream);
    ParamBox b;
    b.store(Param::Distance, 0.9);                       // the recall must replace it by the default
    CHECK(readState(&stream, b) == 1);
    CHECK(b.plain().power);
    CHECK(std::fabs(b.plain().metres - kDefaultMetres) < 1e-12);
}
