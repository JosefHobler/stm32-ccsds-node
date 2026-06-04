#include "ccsds/codec.hpp"

#include <stdexcept>

// Stubs only — real bodies land in Steps 5 (crc16) and 6 (encode/decode).
// They throw / return nullopt so any accidental caller fails loudly instead
// of silently emitting an empty frame.
namespace ccsds {

std::uint16_t crc16(std::span<const std::byte>) noexcept {
    return 0;
}

std::vector<std::byte> encode(const Packet&) {
    throw std::logic_error("ccsds::encode not implemented yet");
}

std::optional<Packet> decode(std::span<const std::byte>) noexcept {
    return std::nullopt;
}

}  // namespace ccsds
