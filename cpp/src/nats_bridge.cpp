#include "ccsds/nats_bridge.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <sstream>
#include <string>

// Forward decl so NatsBridge::bridge() below can take this function's
// address; the definition lives below the ccsds namespace.
extern "C" void ccsds_nats_on_raw_message_thunk(natsConnection* nc,
                                                natsSubscription* sub,
                                                natsMsg* msg,
                                                void* closure);

namespace ccsds {

void NatsBridge::ConnDeleter::operator()(natsConnection* c) const noexcept {
    if (c) {
        natsConnection_Close(c);
        natsConnection_Destroy(c);
    }
}

void NatsBridge::SubDeleter::operator()(natsSubscription* s) const noexcept {
    if (s) {
        natsSubscription_Unsubscribe(s);
        natsSubscription_Destroy(s);
    }
}

namespace {

void check(natsStatus s, std::string_view what) {
    if (s != NATS_OK) {
        std::ostringstream os;
        os << "NATS error in " << what << ": " << natsStatus_GetText(s);
        throw NatsError(os.str());
    }
}

std::string to_hex(std::span<const std::byte> bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(bytes.size() * 2);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto b = std::to_integer<std::uint8_t>(bytes[i]);
        out[2 * i]     = kHex[(b >> 4) & 0x0F];
        out[2 * i + 1] = kHex[b & 0x0F];
    }
    return out;
}

}  // namespace

NatsBridge::NatsBridge(const std::string& url) {
    natsConnection* raw = nullptr;
    check(natsConnection_ConnectTo(&raw, url.c_str()), "ConnectTo");
    conn_.reset(raw);
}

NatsBridge::~NatsBridge() = default;

std::string NatsBridge::to_json(const Packet& pkt) {
    nlohmann::json j;
    j["type"]        = (pkt.type == PacketType::TC ? "TC" : "TM");
    j["apid"]        = static_cast<std::uint16_t>(pkt.apid);
    j["seq_flags"]   = static_cast<std::uint8_t>(pkt.seq_flags);
    j["seq_count"]   = pkt.seq_count;
    j["payload_hex"] = to_hex(pkt.payload);
    return j.dump();
}

void NatsBridge::on_raw_message(natsConnection* /*nc*/,
                                natsSubscription* /*sub*/,
                                natsMsg* msg,
                                void* closure) {
    auto* self = static_cast<NatsBridge*>(closure);
    const char*       data = natsMsg_GetData(msg);
    const int         len  = natsMsg_GetDataLength(msg);

    std::span<const std::byte> frame(
        reinterpret_cast<const std::byte*>(data),
        static_cast<std::size_t>(len));

    if (auto pkt = decode(frame); pkt.has_value()) {
        const std::string body = to_json(*pkt);
        const natsStatus st = natsConnection_Publish(
            self->conn_.get(),
            self->parsed_subject_.c_str(),
            body.data(),
            static_cast<int>(body.size()));
        if (st == NATS_OK) {
            self->parsed_count_.fetch_add(1, std::memory_order_relaxed);
        } else {
            self->dropped_count_.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        self->dropped_count_.fetch_add(1, std::memory_order_relaxed);
    }
    natsMsg_Destroy(msg);
}

void NatsBridge::bridge(std::string_view raw_subject,
                        std::string_view parsed_subject) {
    parsed_subject_.assign(parsed_subject);
    natsSubscription* raw_sub = nullptr;
    const std::string subj(raw_subject);
    check(natsConnection_Subscribe(&raw_sub, conn_.get(),
                                   subj.c_str(),
                                   &::ccsds_nats_on_raw_message_thunk,
                                   this),
          "Subscribe");
    sub_.reset(raw_sub);
}

void NatsBridge::publish_raw(std::string_view subject,
                             std::span<const std::byte> frame) {
    const std::string subj(subject);
    check(natsConnection_Publish(conn_.get(), subj.c_str(),
                                 frame.data(), static_cast<int>(frame.size())),
          "Publish (raw)");
    check(natsConnection_Flush(conn_.get()), "Flush (raw)");
}

void NatsBridge::publish_parsed(std::string_view subject, const Packet& pkt) {
    const std::string subj(subject);
    const std::string body = to_json(pkt);
    check(natsConnection_Publish(conn_.get(), subj.c_str(),
                                 body.data(), static_cast<int>(body.size())),
          "Publish (parsed)");
    check(natsConnection_Flush(conn_.get()), "Flush (parsed)");
}

}  // namespace ccsds

// extern "C" thunk: cnats expects a C function pointer for the message
// handler. Forwards to the static member, which holds the real logic.
extern "C" void ccsds_nats_on_raw_message_thunk(natsConnection* nc,
                                                natsSubscription* sub,
                                                natsMsg* msg,
                                                void* closure) {
    ccsds::NatsBridge::on_raw_message(nc, sub, msg, closure);
}
