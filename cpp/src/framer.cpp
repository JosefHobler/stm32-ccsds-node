#include "ccsds/framer.hpp"

#include <vector>

namespace ccsds {

void Framer::push(std::span<const std::byte> bytes) {
    buf_.insert(buf_.end(), bytes.begin(), bytes.end());
}

std::optional<Packet> Framer::next() {
    while (true) {
        if (buf_.size() < kMinFrameLen) {
            return std::nullopt;
        }

        // Peek the length field. data_field includes the trailing CRC.
        const auto pkt_len = static_cast<std::uint16_t>(
            (std::to_integer<std::uint16_t>(buf_[4]) << 8) |
            std::to_integer<std::uint16_t>(buf_[5]));
        const std::size_t total = kPrimaryHeaderLen +
                                   static_cast<std::size_t>(pkt_len) + 1u;

        // Length field claims an impossible frame — drop one byte and resync.
        // (The firmware buffer caps at kMaxFrameLen too, so anything larger
        // is misalignment, not a giant payload.)
        if (total > kMaxFrameLen) {
            buf_.pop_front();
            ++slipped_;
            continue;
        }

        // Don't have the whole frame yet — hold and wait for more bytes.
        if (buf_.size() < total) {
            return std::nullopt;
        }

        // Materialise into a contiguous view for the decoder. std::deque is
        // not contiguous so we have to copy here; the staging buffer is
        // bounded by kMaxFrameLen (a few hundred bytes), so it's fine.
        std::vector<std::byte> staging;
        staging.reserve(total);
        for (std::size_t i = 0; i < total; ++i) staging.push_back(buf_[i]);

        if (auto pkt = decode(staging); pkt.has_value()) {
            for (std::size_t i = 0; i < total; ++i) buf_.pop_front();
            return pkt;
        }

        // CRC or version mismatch at the candidate alignment. Slip 1 byte
        // and try again — mirrors the firmware's resync strategy and gives
        // ~1/65536 false-positive rate per slip for CCITT-16.
        buf_.pop_front();
        ++slipped_;
    }
}

}  // namespace ccsds
