
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

enum class SeqFlags : std::uint8_t {
    CONTINUATION = 0b00,
    FIRST        = 0b01,
    LAST         = 0b10,
    UNSEGMENTED  = 0b11,
};

inline constexpr std::size_t kPrimaryHeaderLen = 6;
inline constexpr std::size_t kCrcLen           = 2;
inline constexpr std::size_t kMinFrameLen      = kPrimaryHeaderLen + kCrcLen;

inline constexpr std::size_t kMaxFrameLen = 256;

struct Packet {
    PacketType                  type{PacketType::TM};
    Apid                        apid{Apid::IDLE};
    SeqFlags                    seq_flags{SeqFlags::UNSEGMENTED};
    std::uint16_t               seq_count{0};   // 14-bit
    std::vector<std::byte>      payload{};
};

[[nodiscard]] std::uint16_t crc16(std::span<const std::byte> data) noexcept;

[[nodiscard]] std::vector<std::byte> encode(const Packet& packet);

[[nodiscard]] std::optional<Packet> decode(
    std::span<const std::byte> frame) noexcept;

}  
