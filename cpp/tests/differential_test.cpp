#include <catch2/catch_test_macros.hpp>

#include "ccsds/codec.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CCSDS_GENERATED_FIXTURES_JSON
#error "CCSDS_GENERATED_FIXTURES_JSON not set — see cpp/CMakeLists.txt"
#endif

namespace {

std::vector<std::byte> from_hex(const std::string& s) {
    if (s.size() % 2 != 0) {
        throw std::invalid_argument("hex string has odd length");
    }
    std::vector<std::byte> out;
    out.reserve(s.size() / 2);
    auto nyb = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        throw std::invalid_argument("not a hex char");
    };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int hi = nyb(s[i]);
        const int lo = nyb(s[i + 1]);
        out.push_back(std::byte{static_cast<std::uint8_t>((hi << 4) | lo)});
    }
    return out;
}

}  

TEST_CASE("decode-re-encode of Python-generated frames "
          "is byte-identical",
          "[differential][cross-impl]") {
    std::ifstream f(CCSDS_GENERATED_FIXTURES_JSON);
    REQUIRE(f.is_open());
    nlohmann::json doc;
    f >> doc;
    REQUIRE(doc.contains("fixtures"));
    REQUIRE(doc["fixtures"].is_array());
    REQUIRE(!doc["fixtures"].empty());

    std::size_t checked = 0;
    for (const auto& fx : doc["fixtures"]) {
        const auto frame_bytes   = from_hex(fx.at("frame").get<std::string>());
        const auto payload_bytes = from_hex(fx.at("payload").get<std::string>());
        const auto apid          = fx.at("apid").get<std::uint16_t>();
        const auto type          = fx.at("type").get<std::uint8_t>();
        const auto seq           = fx.at("seq").get<std::uint16_t>();

        INFO("fixture index " << checked
             << " apid=" << apid
             << " type=" << static_cast<unsigned>(type)
             << " seq="  << seq
             << " payload_size=" << payload_bytes.size());

        const auto pkt = ccsds::decode(frame_bytes);
        REQUIRE(pkt.has_value());
        REQUIRE(static_cast<std::uint16_t>(pkt->apid) == apid);
        REQUIRE(static_cast<std::uint8_t>(pkt->type)  == type);
        REQUIRE(pkt->seq_count == seq);
        REQUIRE(pkt->payload   == payload_bytes);

        const auto re_encoded = ccsds::encode(*pkt);
        REQUIRE(re_encoded == frame_bytes);
        ++checked;
    }
    INFO("total fixtures verified: " << checked);
}
