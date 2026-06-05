// Streaming framer: recover CCSDS packets from a UART-like byte stream.
//
// The wire has no preamble or escape — alignment is recovered from the
// length field and CRC, with 1-byte slip on every failure (the same
// strategy the firmware's `telecommand_task` uses on RX). Push raw bytes
// in any chunking; pull complete packets out one at a time.
#pragma once

#include "ccsds/codec.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <span>

namespace ccsds {

class Framer {
public:
    Framer() = default;

    // Append stream bytes. Cheap — no parsing happens here.
    void push(std::span<const std::byte> bytes);

    // Try to extract the next complete, CRC-valid packet from the buffer.
    // Returns std::nullopt when the buffer doesn't yet contain a full
    // packet (caller should push more bytes and try again).
    [[nodiscard]] std::optional<Packet> next();

    // Bytes currently held in the internal buffer — useful for tests and
    // for monitoring framer health (a steadily growing buffer means the
    // peer is emitting garbage and the framer is slipping every byte).
    [[nodiscard]] std::size_t buffered() const noexcept { return buf_.size(); }

    // Total bytes the framer dropped because they couldn't form a valid
    // packet at the head of the buffer. A counter that ticks up
    // monotonically — useful as a noise / link-quality signal.
    [[nodiscard]] std::size_t slipped() const noexcept { return slipped_; }

private:
    std::deque<std::byte> buf_{};
    std::size_t           slipped_{0};
};

}  // namespace ccsds
