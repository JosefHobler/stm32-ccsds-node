
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

    void bridge(std::string_view raw_subject,
                std::string_view parsed_subject);

    void publish_raw(std::string_view subject,
                     std::span<const std::byte> frame);

    void publish_parsed(std::string_view subject, const Packet& pkt);

    [[nodiscard]] std::size_t parsed_count() const noexcept {
        return parsed_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t dropped_count() const noexcept {
        return dropped_count_.load(std::memory_order_relaxed);
    }

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

}  
