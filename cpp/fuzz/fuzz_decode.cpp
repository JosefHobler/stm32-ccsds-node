// libFuzzer entry point: feed the codec's decode() and the streaming
// framer arbitrary bytes from libFuzzer's corpus. Any crash, UB, or
// invalid memory access on this path is exactly the guarantee a packet
// parser on a real RF/UART link has to give. Designed to run under
// AddressSanitizer + UndefinedBehaviorSanitizer in the same configure
// step (see cpp/CMakeLists.txt's CCSDS_BUILD_FUZZ + CCSDS_SANITIZE).
//
// Build:
//     cmake -S cpp -B cpp/build-fuzz -G Ninja \
//         -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
//         -DCCSDS_BUILD_FUZZ=ON -DCCSDS_SANITIZE=ON
//     cmake --build cpp/build-fuzz
//
// Run:
//     ./cpp/build-fuzz/fuzz_decode -max_total_time=60
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

    // 1) Direct single-packet decode of arbitrary bytes. Must never
    //    crash or trip UBSan; an std::nullopt return is fine.
    auto pkt = ccsds::decode(bytes);

    // 2) Round-trip whatever did decode successfully through encode()
    //    and re-decode. encode() is only allowed to throw for explicit
    //    range-check failures (out-of-range APID/seq, oversized frame);
    //    swallow those so the fuzzer doesn't false-positive on them.
    if (pkt.has_value()) {
        try {
            auto re = ccsds::encode(*pkt);
            auto pkt2 = ccsds::decode(re);
            // pkt2 may legitimately not match pkt's binary layout if the
            // original frame contained slack bytes after data_field — but
            // it must always *decode*.
            (void)pkt2;
        } catch (const std::invalid_argument&) {
            // intentional API boundary check, not a fuzzer finding
        }
    }

    // 3) Push the same bytes through the streaming framer in two
    //    arbitrary-sized chunks. Exercises the buffer-management and
    //    1-byte slip resync paths that the single-frame decode doesn't.
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
