#include "ccsds/codec.hpp"

#include <stdexcept>

namespace ccsds {

// CRC-16/CCITT-FALSE (a.k.a. CRC-16/IBM-3740): poly 0x1021, init 0xFFFF,
// refin false, refout false, xorout 0x0000. Byte-by-byte MSB-first
// algorithm, matching the firmware bit-for-bit. No lookup table — at host
// throughput it's not worth the cache pressure or the extra TU.
std::uint16_t crc16(std::span<const std::byte> data) noexcept {
    std::uint16_t crc = 0xFFFFu;
    for (auto byte : data) {
        const auto in = std::to_integer<std::uint16_t>(byte);
        crc = static_cast<std::uint16_t>(crc ^ static_cast<std::uint16_t>(in << 8));
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u)
                      ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u)
                      : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::vector<std::byte> encode(const Packet&) {
    throw std::logic_error("ccsds::encode not implemented yet");
}

std::optional<Packet> decode(std::span<const std::byte>) noexcept {
    return std::nullopt;
}

}  // namespace ccsds
