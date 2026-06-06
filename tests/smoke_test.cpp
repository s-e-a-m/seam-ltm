#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

TEST_CASE("doctest harness is alive") {
    CHECK(1 + 1 == 2);
}
