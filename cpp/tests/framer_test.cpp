#include <catch2/catch_test_macros.hpp>

#include "ccsds/codec.hpp"
#include "ccsds/framer.hpp"
#include "fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

template <std::size_t N>
std::vector<std::byte> to_bytes(const std::array<std::uint8_t, N>& a) {
    std::vector<std::byte> v;
    v.reserve(N);
    for (auto b : a) v.push_back(std::byte{b});
    return v;
}

std::span<const std::byte> view(const std::vector<std::byte>& v) {
    return std::span<const std::byte>(v.data(), v.size());
}

}  

TEST_CASE("framer: one complete frame in one push", "[framer]") {
    ccsds::Framer f;
    const auto frame = to_bytes(ccsds::fixtures::ping_tc_frame);
    f.push(view(frame));
    const auto pkt = f.next();
    REQUIRE(pkt.has_value());
    REQUIRE(pkt->apid == ccsds::Apid::CMD);
    REQUIRE(f.next() == std::nullopt);
    REQUIRE(f.buffered() == 0);
    REQUIRE(f.slipped()  == 0);
}

TEST_CASE("framer: a frame split across two pushes is recovered",
          "[framer][partial]") {
    ccsds::Framer f;
    const auto frame = to_bytes(ccsds::fixtures::hk_tm_frame);
    f.push(std::span<const std::byte>(frame.data(), 3));
    REQUIRE(f.next() == std::nullopt);
    f.push(std::span<const std::byte>(frame.data() + 3, 10));
    REQUIRE(f.next() == std::nullopt);
    f.push(std::span<const std::byte>(frame.data() + 13,
                                      frame.size() - 13));
    const auto pkt = f.next();
    REQUIRE(pkt.has_value());
    REQUIRE(pkt->apid == ccsds::Apid::HK);
    REQUIRE(f.slipped() == 0);
}

TEST_CASE("framer: garbage prefix is skipped byte-by-byte", "[framer][slip]") {
    ccsds::Framer f;
    std::vector<std::byte> stream;
    stream.push_back(std::byte{0xAA});
    stream.push_back(std::byte{0x55});
    stream.push_back(std::byte{0xFF});
    const auto frame = to_bytes(ccsds::fixtures::ping_tc_frame);
    stream.insert(stream.end(), frame.begin(), frame.end());

    f.push(view(stream));
    const auto pkt = f.next();
    REQUIRE(pkt.has_value());
    REQUIRE(pkt->apid == ccsds::Apid::CMD);
    REQUIRE(f.slipped() >= 1);
    REQUIRE(f.next() == std::nullopt);
}

TEST_CASE("framer: corrupted frame followed by a valid frame",
          "[framer][corruption]") {
    ccsds::Framer f;
    auto bad   = to_bytes(ccsds::fixtures::ping_tc_frame);
    bad.back() = std::byte{static_cast<std::uint8_t>(
        std::to_integer<std::uint8_t>(bad.back()) ^ 0xFFu)};
    const auto good = to_bytes(ccsds::fixtures::set_tm_rate_tc_frame);

    std::vector<std::byte> stream;
    stream.insert(stream.end(), bad.begin(),  bad.end());
    stream.insert(stream.end(), good.begin(), good.end());
    f.push(view(stream));

    const auto pkt = f.next();
    REQUIRE(pkt.has_value());
    REQUIRE(pkt->apid == ccsds::Apid::CMD);
    REQUIRE(pkt->seq_count == 2);
    REQUIRE(f.slipped() >= 1);
}

TEST_CASE("framer: two valid frames back-to-back", "[framer]") {
    ccsds::Framer f;
    const auto a = to_bytes(ccsds::fixtures::hk_tm_frame);
    const auto b = to_bytes(ccsds::fixtures::event_tm_frame);
    std::vector<std::byte> stream;
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), b.begin(), b.end());
    f.push(view(stream));

    const auto p1 = f.next();
    REQUIRE(p1.has_value());
    REQUIRE(p1->apid == ccsds::Apid::HK);

    const auto p2 = f.next();
    REQUIRE(p2.has_value());
    REQUIRE(p2->apid == ccsds::Apid::EVENT);

    REQUIRE(f.next() == std::nullopt);
    REQUIRE(f.buffered() == 0);
    REQUIRE(f.slipped()  == 0);
}

TEST_CASE("framer: truncated frame is held until more bytes arrive",
          "[framer][partial]") {
    ccsds::Framer f;
    const auto frame = to_bytes(ccsds::fixtures::set_tm_rate_tc_frame);
    f.push(std::span<const std::byte>(frame.data(), frame.size() - 1));
    REQUIRE(f.next() == std::nullopt);
    REQUIRE(f.buffered() == frame.size() - 1);

    f.push(std::span<const std::byte>(frame.data() + frame.size() - 1, 1));
    const auto pkt = f.next();
    REQUIRE(pkt.has_value());
    REQUIRE(pkt->apid == ccsds::Apid::CMD);
}
