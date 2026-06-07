#include "ccsds/codec.hpp"
#include "ccsds/framer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(data), size);

    auto pkt = ccsds::decode(bytes);

    if (pkt.has_value()) {
        try {
            auto re = ccsds::encode(*pkt);
            auto pkt2 = ccsds::decode(re);
            (void)pkt2; 
        } catch (const std::invalid_argument&) {
        }
    }

    if (size > 0) {
        const std::size_t mid = size / 2;
        ccsds::Framer framer;
        framer.push(bytes.first(mid));
        while (auto p = framer.next()) { (void)p; }
        framer.push(bytes.subspan(mid));
        while (auto p = framer.next()) { (void)p; }
    }

    return 0;
}
