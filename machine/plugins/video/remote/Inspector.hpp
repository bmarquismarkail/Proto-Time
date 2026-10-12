#pragma once
#include "machine/plugins/debug/DebugService.hpp"
#include "machine/plugins/IoPlugin.hpp"
#include <nlohmann/json.hpp>

namespace BMMQ::RemoteVideo {
// Owner-lane capture; every member owns its storage. No borrowed Machine data.
struct Snapshot {
    Debug::Reply cpu;
    RealtimeVideoPacket frame;
    VideoStateView video;
    std::uint64_t sequence{}, cycles{};
};
// One producer/consumer. Bounded latest-only exchange, with explicit loss count.
class Mailbox {
    static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
    std::array<Snapshot,3> slots_{};
    std::atomic<std::uint8_t> shared_{2};
    std::uint8_t producer_{0},consumer_{1};
    std::atomic<std::uint64_t> drops_{0};
public:
    Mailbox()=default;
    Mailbox(const Mailbox&)=delete;
    bool publish(Snapshot&&) noexcept;
    std::optional<Snapshot> consume() noexcept;
    std::uint64_t dropped() const noexcept {return drops_.load(std::memory_order_relaxed);}
};
// All decoding and JSON allocation runs on the inspection/presentation lane.
class Inspector {
    std::string core_;
public:
    explicit Inspector(std::string core);
    nlohmann::json decode(const Snapshot&,std::uint64_t dropped) const;
    static Debug::Command command(const nlohmann::json&,std::uint32_t id);
    static nlohmann::json reply(const Debug::Reply&);
};
}
