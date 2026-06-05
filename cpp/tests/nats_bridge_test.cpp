// NATS bridge round-trip: publish a raw CCSDS frame on `tc.raw`, the
// bridge should decode it and republish a JSON view on `tc.parsed`.
// Run against a local nats-server; the test is skipped (SUCCEED) when
// NATS_URL is not set, so the default build doesn't need a broker.
#include <catch2/catch_test_macros.hpp>

#include "ccsds/codec.hpp"
#include "ccsds/nats_bridge.hpp"
#include "fixtures.hpp"

#include <nats.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

template <std::size_t N>
std::vector<std::byte> to_bytes(const std::array<std::uint8_t, N>& a) {
    std::vector<std::byte> v;
    v.reserve(N);
    for (auto b : a) v.push_back(std::byte{b});
    return v;
}

const char* nats_url() {
    if (const char* u = std::getenv("NATS_URL"); u && *u) return u;
    return nullptr;
}

}  // namespace

TEST_CASE("NATS bridge: tc.raw -> decode -> tc.parsed round trip",
          "[nats][integration]") {
    const char* url = nats_url();
    if (!url) {
        WARN("NATS_URL not set — skipping NATS integration test");
        SUCCEED();
        return;
    }

    ccsds::NatsBridge bridge(url);
    bridge.bridge("tc.raw", "tc.parsed");

    // Independent listener on tc.parsed so we can capture exactly what the
    // bridge republishes.
    natsConnection* listener_raw = nullptr;
    REQUIRE(natsConnection_ConnectTo(&listener_raw, url) == NATS_OK);
    struct ListenerGuard {
        natsConnection* c{};
        ~ListenerGuard() { if (c) { natsConnection_Close(c); natsConnection_Destroy(c); } }
    } listener{listener_raw};

    natsSubscription* sync_sub_raw = nullptr;
    REQUIRE(natsConnection_SubscribeSync(&sync_sub_raw, listener.c,
                                         "tc.parsed") == NATS_OK);
    struct SubGuard {
        natsSubscription* s{};
        ~SubGuard() { if (s) { natsSubscription_Unsubscribe(s); natsSubscription_Destroy(s); } }
    } sync_sub{sync_sub_raw};
    // Round-trip the SUB to the server before the bridge has a chance to
    // publish on tc.parsed — otherwise the parsed message can arrive
    // before our listener's subscription is registered and be dropped.
    REQUIRE(natsConnection_Flush(listener.c) == NATS_OK);

    // Publish PING TC raw frame on tc.raw via a fresh publisher connection
    // — exercises the bridge's subscribe path end-to-end.
    natsConnection* pub_raw = nullptr;
    REQUIRE(natsConnection_ConnectTo(&pub_raw, url) == NATS_OK);
    struct PubGuard {
        natsConnection* c{};
        ~PubGuard() { if (c) { natsConnection_Close(c); natsConnection_Destroy(c); } }
    } pub{pub_raw};

    const auto frame = to_bytes(ccsds::fixtures::ping_tc_frame);
    REQUIRE(natsConnection_Publish(pub.c, "tc.raw",
                                   frame.data(),
                                   static_cast<int>(frame.size())) == NATS_OK);
    REQUIRE(natsConnection_Flush(pub.c) == NATS_OK);

    // Wait for the bridge to land a message on tc.parsed.
    natsMsg* msg = nullptr;
    REQUIRE(natsSubscription_NextMsg(&msg, sync_sub.s, 5000) == NATS_OK);
    REQUIRE(msg != nullptr);

    const std::string body(natsMsg_GetData(msg),
                           static_cast<std::size_t>(natsMsg_GetDataLength(msg)));
    natsMsg_Destroy(msg);

    const auto j = nlohmann::json::parse(body);
    REQUIRE(j.at("type").get<std::string>() == "TC");
    REQUIRE(j.at("apid").get<std::uint16_t>() ==
            static_cast<std::uint16_t>(ccsds::Apid::CMD));
    REQUIRE(j.at("seq_count").get<std::uint16_t>() == 1);
    REQUIRE(j.at("payload_hex").get<std::string>() == "01");

    // Internal counters should reflect the one parsed publish.
    REQUIRE(bridge.parsed_count() >= 1);
}

TEST_CASE("NATS bridge: malformed raw frame is dropped, not republished",
          "[nats][integration]") {
    const char* url = nats_url();
    if (!url) {
        WARN("NATS_URL not set — skipping NATS integration test");
        SUCCEED();
        return;
    }

    ccsds::NatsBridge bridge(url);
    bridge.bridge("tc.raw.bad", "tc.parsed.bad");

    natsConnection* pub_raw = nullptr;
    REQUIRE(natsConnection_ConnectTo(&pub_raw, url) == NATS_OK);
    struct PubGuard {
        natsConnection* c{};
        ~PubGuard() { if (c) { natsConnection_Close(c); natsConnection_Destroy(c); } }
    } pub{pub_raw};

    // 3 bytes of pure garbage — below the minimum frame size.
    const std::array<std::byte, 3> junk = {std::byte{0}, std::byte{0}, std::byte{0}};
    REQUIRE(natsConnection_Publish(pub.c, "tc.raw.bad",
                                   junk.data(),
                                   static_cast<int>(junk.size())) == NATS_OK);
    REQUIRE(natsConnection_Flush(pub.c) == NATS_OK);

    // Give the bridge a beat to process and update its counter.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(bridge.parsed_count()  == 0);
    REQUIRE(bridge.dropped_count() >= 1);
}
