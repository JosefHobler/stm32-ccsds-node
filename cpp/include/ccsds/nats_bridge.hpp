// Small NATS TM/TC bridge.
//
// Demonstrates the shape of real ground-segment messaging: take raw CCSDS
// frames off a transport-level subject (`tc.raw`, `tm.raw`), decode them
// with the codec in this same library, and republish a structured view of
// each packet on a parsed subject (`tc.parsed`, `tm.parsed`) for downstream
// consumers (dashboards, archivers, alerting).
//
// The bridge is opt-in: the NATS C client is only linked when the CMake
// option `CCSDS_WITH_NATS=ON` is set. With it OFF, this header isn't
// compiled into the library and downstream users have no extra deps.
#pragma once

#include "ccsds/codec.hpp"

#include <nats.h>

#include <atomic>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ccsds {

// Thrown when the underlying cnats call returns a non-OK status.
class NatsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class NatsBridge {
public:
    explicit NatsBridge(const std::string& url);
    ~NatsBridge();

    NatsBridge(const NatsBridge&)            = delete;
    NatsBridge& operator=(const NatsBridge&) = delete;
    NatsBridge(NatsBridge&&)                 = delete;
    NatsBridge& operator=(NatsBridge&&)      = delete;

    // Subscribe to `raw_subject` and republish each decoded packet's
    // structured fields as JSON on `parsed_subject`. Frames that fail to
    // decode are silently dropped — the CRC already told us they're noise.
    //
    // The CCSDS streaming framer is *not* used here: NATS messages arrive
    // as bounded discrete payloads, which is the single-packet decode()
    // path, not the recover-from-stream path. (A real ground segment using
    // NATS would put framing at the gateway, not on the bus.)
    void bridge(std::string_view raw_subject,
                std::string_view parsed_subject);

    // Publish raw wire bytes on a subject. Mirrors the gateway side.
    void publish_raw(std::string_view subject,
                     std::span<const std::byte> frame);

    // Publish the parsed JSON view of a Packet on a subject. Useful when
    // a producer is local and doesn't need to round-trip through frames.
    void publish_parsed(std::string_view subject, const Packet& pkt);

    // Counters — exposed mainly for tests and link-health monitoring.
    [[nodiscard]] std::size_t parsed_count() const noexcept {
        return parsed_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t dropped_count() const noexcept {
        return dropped_count_.load(std::memory_order_relaxed);
    }

    // Public because cnats's natsMsgHandler takes a C function pointer;
    // an extern "C" thunk in nats_bridge.cpp calls into this. Not
    // intended for end-user code.
    static void on_raw_message(natsConnection* nc,
                               natsSubscription* sub,
                               natsMsg* msg,
                               void* closure);

private:
    static std::string to_json(const Packet& pkt);

    struct ConnDeleter { void operator()(natsConnection*) const noexcept; };
    struct SubDeleter  { void operator()(natsSubscription*) const noexcept; };

    std::unique_ptr<natsConnection,   ConnDeleter> conn_;
    std::unique_ptr<natsSubscription, SubDeleter>  sub_;
    std::string                                    parsed_subject_;

    std::atomic<std::size_t> parsed_count_{0};
    std::atomic<std::size_t> dropped_count_{0};
};

}  // namespace ccsds
