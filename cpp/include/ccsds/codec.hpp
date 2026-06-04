// Host-side modern C++20 CCSDS Space Packet codec.
//
// Wire format (matches the STM32 firmware and Python ground codec):
//
//   [ packet_id(2) | seq_ctl(2) | data_len(2) | payload | crc16(2) ]
//
//   packet_id = vvv t s aaaaaaaaaaa
//               ^   ^ ^ ^ APID (11 bits)
//               |   | sec-hdr flag (1 bit)
//               |   type: 0=TM, 1=TC (1 bit)
//               version (3 bits, always 0)
//
//   seq_ctl   = ff cccccccccccccc   (2-bit grouping flags, 14-bit count)
//   data_len  = (payload_octets + crc_octets) - 1  big-endian
//   crc16     = CRC-16/CCITT-FALSE over header+payload
//               (poly 0x1021, init 0xFFFF, no refin/refout, no xorout;
//                check value for "123456789" = 0x29B1)
//
// This header is the interface contract. Logic lands in Steps 5 and 6.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ccsds {

enum class PacketType : std::uint8_t {
    TM = 0,
    TC = 1,
};

enum class Apid : std::uint16_t {
    HK    = 0x064,
    EVENT = 0x065,
    CMD   = 0x0C8,
    IDLE  = 0x7FF,
};

enum class Command : std::uint8_t {
    PING        = 0x01,
    REBOOT      = 0x02,
    SET_TM_RATE = 0x03,
    GET_VERSION = 0x04,
    LED_CTRL    = 0x05,
};

// CCSDS sequence flags. The firmware emits all packets as UNSEGMENTED (0b11).
enum class SeqFlags : std::uint8_t {
    CONTINUATION = 0b00,
    FIRST        = 0b01,
    LAST         = 0b10,
    UNSEGMENTED  = 0b11,
};

// On-wire sizes are part of the contract.
inline constexpr std::size_t kPrimaryHeaderLen = 6;
inline constexpr std::size_t kCrcLen           = 2;
inline constexpr std::size_t kMinFrameLen      = kPrimaryHeaderLen + kCrcLen;

// Hard upper bound mirrors the firmware buffer (CCSDS_MAX_PACKET_LEN).
inline constexpr std::size_t kMaxFrameLen = 256;

// A decoded packet. Storage is owned (std::vector) so a Packet remains valid
// after the source buffer goes out of scope — important for the streaming
// framer and the NATS bridge that hand packets across thread/job boundaries.
struct Packet {
    PacketType                  type{PacketType::TM};
    Apid                        apid{Apid::IDLE};
    SeqFlags                    seq_flags{SeqFlags::UNSEGMENTED};
    std::uint16_t               seq_count{0};   // 14-bit
    std::vector<std::byte>      payload{};
};

// CRC-16/CCITT-FALSE.
[[nodiscard]] std::uint16_t crc16(std::span<const std::byte> data) noexcept;

// Encode a Packet to the wire. Throws std::invalid_argument if any field is
// out of range (APID > 0x7FF, seq_count > 0x3FFF, payload too large for
// kMaxFrameLen). Boundary-checked at the API edge per project policy.
[[nodiscard]] std::vector<std::byte> encode(const Packet& packet);

// Strict single-frame decode. Returns std::nullopt on any failure
// (truncation, version mismatch, length-field inconsistency, CRC mismatch).
// Typed failure, never exceptions, so the streaming framer can use this as
// a cheap validity probe.
[[nodiscard]] std::optional<Packet> decode(
    std::span<const std::byte> frame) noexcept;

}  // namespace ccsds
