#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "alogos_state.h"
#include "public.sdk/source/common/memorystream.h"
#include <limits>

using namespace alogos;
using Steinberg::IBStream;
using Steinberg::MemoryStream;

static void rewindStream(MemoryStream& s) { Steinberg::int64 p = 0; s.seek(0, IBStream::kIBSeekSet, &p); }

TEST_CASE("the processor's state round-trips every parameter") {
    ParamBox a;
    a.store(Param::Power, 1.0);
    a.store(Param::StoneId, stoneIdToNormalized(5));
    a.store(Param::Distance, distanceToNormalized(4.25));
    a.store(Param::Sections, sectionsToNormalized(61));
    a.store(Param::Scale, scaleToNormalized(3));
    a.store(Param::Density, densityToNormalized(2));
    a.store(Param::Gain, gainToNormalized(4));
    MemoryStream stream;
    writeState(&stream, a);
    rewindStream(stream);
    ParamBox b;
    CHECK(readState(&stream, b) == kNumParams);
    for (int i = 0; i < kNumParams; ++i) CHECK(b.normalized((Param)i) == a.normalized((Param)i));
    const Plain p = b.plain();
    CHECK(p.power);
    CHECK(p.stoneId == 5);
    CHECK(p.metres == doctest::Approx(4.25));
    CHECK(p.sections == 61);
    CHECK(p.scale == 3);
    CHECK(p.density == 2);
    CHECK(p.gain == 4);
}

TEST_CASE("a short blob of 4 doubles keeps the defaults of the other 3") {
    MemoryStream stream;
    {
        Steinberg::IBStreamer w(&stream, kLittleEndian);
        w.writeDouble(1.0);                              // Power
        w.writeDouble(stoneIdToNormalized(4));           // StoneId
        w.writeDouble(distanceToNormalized(5.0));        // Distance
        w.writeDouble(sectionsToNormalized(40));         // Sections
    }
    rewindStream(stream);
    ParamBox b;
    b.store(Param::Scale, scaleToNormalized(3));         // values the recall must replace by the defaults
    b.store(Param::Density, densityToNormalized(2));
    b.store(Param::Gain, gainToNormalized(4));
    CHECK(readState(&stream, b) == 4);
    const Plain p = b.plain();
    CHECK(p.power);
    CHECK(p.stoneId == 4);
    CHECK(p.metres == doctest::Approx(5.0));
    CHECK(p.sections == 40);
    CHECK(p.scale == 0);
    CHECK(p.density == 0);
    CHECK(p.gain == 0);
}

TEST_CASE("an empty blob gives the defaults") {
    MemoryStream stream;
    ParamBox b;
    b.store(Param::Sections, sectionsToNormalized(99));
    CHECK(readState(&stream, b) == 0);
    const Plain p = b.plain();
    CHECK_FALSE(p.power);
    CHECK(p.sections == kDefaultSections);
    CHECK(p.metres == doctest::Approx(kDefaultMetres).epsilon(1e-12));
}

TEST_CASE("recalled out-of-range and NaN values clamp") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (int pass = 0; pass < 3; ++pass) {
        MemoryStream stream;
        {
            Steinberg::IBStreamer w(&stream, kLittleEndian);
            w.writeDouble(0.0);
            if (pass == 2) {
                for (int i = 1; i < kNumParams; ++i) w.writeDouble(nan);
            } else {
                w.writeDouble(stoneIdToNormalized(pass == 0 ? 9 : -2));
                w.writeDouble(distanceToNormalized(pass == 0 ? 40.0 : -1.0));
                w.writeDouble(sectionsToNormalized(pass == 0 ? 500 : 0));
                w.writeDouble(scaleToNormalized(pass == 0 ? 7 : -7));
                w.writeDouble(densityToNormalized(pass == 0 ? 3 : -3));
                w.writeDouble(gainToNormalized(pass == 0 ? 9 : -9));
            }
        }
        rewindStream(stream);
        ParamBox b;
        CHECK(readState(&stream, b) == kNumParams);
        const Plain p = b.plain();
        if (pass == 0) {
            CHECK(p.stoneId == kMaxStoneId); CHECK(p.metres == kMaxMetres); CHECK(p.sections == kMaxSections);
            CHECK(p.scale == 3); CHECK(p.density == 2); CHECK(p.gain == 4);
        } else if (pass == 1) {
            CHECK(p.stoneId == 0); CHECK(p.metres == kMinMetres); CHECK(p.sections == 1);
            CHECK(p.scale == 0); CHECK(p.density == 0); CHECK(p.gain == 0);
        } else {
            CHECK(p.stoneId == 0); CHECK(p.metres == kDefaultMetres); CHECK(p.sections == kDefaultSections);
            CHECK(p.scale == 0); CHECK(p.density == 0); CHECK(p.gain == 0);
        }
    }
}
