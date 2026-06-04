#include <catch2/catch_test_macros.hpp>

#include "ccsds/codec.hpp"
#include "fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

// Reinterpret a uint8_t array as a span<const std::byte> for the CRC API.
template <std::size_t N>
std::span<const std::byte> as_byte_span(
    const std::array<std::uint8_t, N>& a) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(a.data()), a.size());
}

std::span<const std::byte> as_byte_span(std::string_view s) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size());
}

}  // namespace

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

// --- CRC ------------------------------------------------------------------
//
// CRC-16/CCITT-FALSE conformance: the RevEng catalogue's published check
// value for the variant is 0x29B1 over the ASCII string "123456789".
// Pinning it here means any future change to the CRC implementation that
// silently picks a different variant (XMODEM, KERMIT, ...) fails CI
// loudly instead of producing frames the firmware rejects.

TEST_CASE("crc16 check value", "[crc][conformance]") {
    REQUIRE(ccsds::crc16(as_byte_span("123456789")) == 0x29B1);
}

TEST_CASE("crc16 known edge cases", "[crc]") {
    // Initial register, never xored with anything, returns 0xFFFF.
    REQUIRE(ccsds::crc16(std::span<const std::byte>{}) == 0xFFFF);
    // Single zero byte — independently computed reference.
    constexpr std::array<std::uint8_t, 1> one_zero{0x00};
    REQUIRE(ccsds::crc16(as_byte_span(one_zero)) == 0xE1F0);
}

TEST_CASE("crc16 validates a real HK fixture", "[crc][fixture]") {
    using namespace ccsds::fixtures;
    // The last two bytes of every fixture are the CRC over the rest.
    constexpr std::size_t n = hk_tm_frame.size();
    const std::span<const std::byte> header_and_payload =
        as_byte_span(hk_tm_frame).first(n - ccsds::kCrcLen);
    const std::uint16_t expected =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(hk_tm_frame[n - 2]) << 8) |
            static_cast<std::uint16_t>(hk_tm_frame[n - 1]));
    REQUIRE(ccsds::crc16(header_and_payload) == expected);
}
