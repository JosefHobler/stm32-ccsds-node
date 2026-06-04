#include <catch2/catch_test_macros.hpp>

#include "ccsds/codec.hpp"

#include <cstdint>

// Step 4 lands the interface contract only; this exercise pins the parts
// of the contract that are observable today (enum values + on-wire sizes)
// so an accidental change shows up as a CI failure.
TEST_CASE("interface contract: enum values", "[contract]") {
    REQUIRE(static_cast<std::uint8_t>(ccsds::PacketType::TM) == 0);
    REQUIRE(static_cast<std::uint8_t>(ccsds::PacketType::TC) == 1);

    REQUIRE(static_cast<std::uint16_t>(ccsds::Apid::HK)    == 0x064);
    REQUIRE(static_cast<std::uint16_t>(ccsds::Apid::EVENT) == 0x065);
    REQUIRE(static_cast<std::uint16_t>(ccsds::Apid::CMD)   == 0x0C8);
    REQUIRE(static_cast<std::uint16_t>(ccsds::Apid::IDLE)  == 0x7FF);

    REQUIRE(static_cast<std::uint8_t>(ccsds::Command::PING)        == 0x01);
    REQUIRE(static_cast<std::uint8_t>(ccsds::Command::REBOOT)      == 0x02);
    REQUIRE(static_cast<std::uint8_t>(ccsds::Command::SET_TM_RATE) == 0x03);
    REQUIRE(static_cast<std::uint8_t>(ccsds::Command::GET_VERSION) == 0x04);
    REQUIRE(static_cast<std::uint8_t>(ccsds::Command::LED_CTRL)    == 0x05);

    REQUIRE(static_cast<std::uint8_t>(ccsds::SeqFlags::UNSEGMENTED) == 0b11);
}

TEST_CASE("interface contract: wire-format sizes", "[contract]") {
    REQUIRE(ccsds::kPrimaryHeaderLen == 6);
    REQUIRE(ccsds::kCrcLen           == 2);
    REQUIRE(ccsds::kMinFrameLen      == 8);
    REQUIRE(ccsds::kMaxFrameLen      == 256);
}
