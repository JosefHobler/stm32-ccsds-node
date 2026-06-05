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
            // Promote crc to unsigned before the shift so the `^ 0x1021u`
            // and the cast back to uint16_t don't trip clang's stricter
            // -Wsign-conversion (the int-promoted result of `crc << 1`
            // would otherwise mix with an unsigned literal).
            const unsigned shifted = static_cast<unsigned>(crc) << 1;
            crc = (crc & 0x8000u)
                      ? static_cast<std::uint16_t>(shifted ^ 0x1021u)
                      : static_cast<std::uint16_t>(shifted);
        }
    }
    return crc;
}

namespace {

constexpr std::uint16_t kApidMask     = 0x07FFu;
constexpr std::uint16_t kSeqCountMask = 0x3FFFu;
constexpr std::uint16_t kVersion      = 0;  // CCSDS Space Packet version 1

void write_be16(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::byte>(v & 0xFFu));
}

std::uint16_t read_be16(std::span<const std::byte> buf, std::size_t off) {
    const auto hi = std::to_integer<std::uint16_t>(buf[off]);
    const auto lo = std::to_integer<std::uint16_t>(buf[off + 1]);
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

}  // namespace

std::vector<std::byte> encode(const Packet& packet) {
    const auto apid = static_cast<std::uint16_t>(packet.apid);
    if (apid > kApidMask) {
        throw std::invalid_argument("ccsds::encode: APID > 0x7FF");
    }
    if (packet.seq_count > kSeqCountMask) {
        throw std::invalid_argument("ccsds::encode: seq_count > 0x3FFF");
    }

    const std::size_t total =
        kPrimaryHeaderLen + packet.payload.size() + kCrcLen;
    if (total > kMaxFrameLen) {
        throw std::invalid_argument("ccsds::encode: frame exceeds kMaxFrameLen");
    }
    // data_field_octets - 1 must fit in 16 bits. The kMaxFrameLen check
    // already enforces this on the host, but keep the firmware's guard
    // intact in case kMaxFrameLen is ever relaxed.
    if (packet.payload.size() + kCrcLen > 0xFFFFu) {
        throw std::invalid_argument("ccsds::encode: payload too large");
    }

    const auto type_bit = static_cast<std::uint16_t>(packet.type);     // 0 or 1
    const auto seq_flag = static_cast<std::uint16_t>(packet.seq_flags); // 0..3
    // sec_hdr_flag is always 0 — the firmware does not emit a secondary header.
    const std::uint16_t pkt_id = static_cast<std::uint16_t>(
        (kVersion & 0x7u) << 13 | (type_bit & 0x1u) << 12 | (apid & kApidMask));
    const std::uint16_t pkt_seq = static_cast<std::uint16_t>(
        (seq_flag & 0x3u) << 14 | (packet.seq_count & kSeqCountMask));
    const std::uint16_t pkt_len = static_cast<std::uint16_t>(
        packet.payload.size() + kCrcLen - 1u);

    std::vector<std::byte> out;
    out.reserve(total);
    write_be16(out, pkt_id);
    write_be16(out, pkt_seq);
    write_be16(out, pkt_len);
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());

    const std::uint16_t crc = crc16(std::span<const std::byte>(out));
    write_be16(out, crc);
    return out;
}

std::optional<Packet> decode(std::span<const std::byte> frame) noexcept {
    if (frame.size() < kMinFrameLen) {
        return std::nullopt;
    }

    const auto pkt_id  = read_be16(frame, 0);
    const auto pkt_seq = read_be16(frame, 2);
    const auto pkt_len = read_be16(frame, 4);

    if (((pkt_id >> 13) & 0x7u) != kVersion) {
        return std::nullopt;
    }

    // data_field includes the trailing CRC.
    const std::size_t data_field =
        static_cast<std::size_t>(pkt_len) + 1u;
    if (data_field < kCrcLen) {
        return std::nullopt;
    }
    const std::size_t total = kPrimaryHeaderLen + data_field;
    if (total > frame.size()) {
        return std::nullopt;
    }

    const std::size_t crc_pos = kPrimaryHeaderLen + (data_field - kCrcLen);
    const std::uint16_t want   = read_be16(frame, crc_pos);
    const std::uint16_t got    = crc16(frame.first(crc_pos));
    if (want != got) {
        return std::nullopt;
    }

    Packet out;
    out.type      = static_cast<PacketType>((pkt_id >> 12) & 0x1u);
    out.apid      = static_cast<Apid>(pkt_id & kApidMask);
    out.seq_flags = static_cast<SeqFlags>((pkt_seq >> 14) & 0x3u);
    out.seq_count = static_cast<std::uint16_t>(pkt_seq & kSeqCountMask);

    const std::size_t payload_len = data_field - kCrcLen;
    // span::iterator's operator+ takes a signed difference_type;
    // explicit ptrdiff_t casts keep clang's -Wsign-conversion quiet.
    const auto hdr  = static_cast<std::ptrdiff_t>(kPrimaryHeaderLen);
    const auto tail = static_cast<std::ptrdiff_t>(kPrimaryHeaderLen + payload_len);
    out.payload.assign(frame.begin() + hdr, frame.begin() + tail);
    return out;
}

}  // namespace ccsds
