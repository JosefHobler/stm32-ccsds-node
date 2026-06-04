#include <catch2/catch_test_macros.hpp>

#include "ccsds/codec.hpp"

TEST_CASE("scaffolding builds and links", "[scaffold]") {
    // Closes the build loop end-to-end before any real codec logic exists.
    REQUIRE(ccsds::scaffolding_sentinel() == 0x29B1);
}
