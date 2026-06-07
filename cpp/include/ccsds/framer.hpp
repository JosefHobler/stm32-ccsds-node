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

    void push(std::span<const std::byte> bytes);

    [[nodiscard]] std::optional<Packet> next();

    [[nodiscard]] std::size_t buffered() const noexcept { return buf_.size(); }

    [[nodiscard]] std::size_t slipped() const noexcept { return slipped_; }

private:
    std::deque<std::byte> buf_{};
    std::size_t           slipped_{0};
};

}  
