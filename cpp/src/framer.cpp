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

        const auto pkt_len = static_cast<std::uint16_t>(
            (std::to_integer<std::uint16_t>(buf_[4]) << 8) |
            std::to_integer<std::uint16_t>(buf_[5]));
        const std::size_t total = kPrimaryHeaderLen +
                                   static_cast<std::size_t>(pkt_len) + 1u;

        if (total > kMaxFrameLen) {
            buf_.pop_front();
            ++slipped_;
            continue;
        }

        if (buf_.size() < total) {
            return std::nullopt;
        }

        std::vector<std::byte> staging;
        staging.reserve(total);
        for (std::size_t i = 0; i < total; ++i) staging.push_back(buf_[i]);

        if (auto pkt = decode(staging); pkt.has_value()) {
            for (std::size_t i = 0; i < total; ++i) buf_.pop_front();
            return pkt;
        }

        buf_.pop_front();
        ++slipped_;
    }
}

}  
