#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "seam_calbus_client.h"

// SEAM_CALBUS_PATH points at the version-99 stub (see tests/CMakeLists.txt).
TEST_CASE("client refuses a dylib with a mismatched version") {
    auto& c = Seam::CalbusClient::instance();
    CHECK_FALSE(c.available());
    CHECK(c.registerSlot() == -1);

    // The stub's snapshot returns 7; a client that fell through the gate would
    // report 7 records that do not exist.
    SeamCalbusRecord out[SEAM_CALBUS_MAX_SLOTS];
    CHECK(c.snapshot(out, SEAM_CALBUS_MAX_SLOTS) == 0);
}
