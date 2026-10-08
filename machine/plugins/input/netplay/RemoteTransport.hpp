#pragma once
#include "LockstepEngine.hpp"
#include <chrono>
#include <memory>
#include <string>

namespace BMMQ::Netplay {
struct RemoteTransportConfig {
    std::string localAddress{"127.0.0.1"}, peerAddress{"127.0.0.1"};
    std::uint16_t localPort{}, peerPort{};
    std::chrono::milliseconds disconnectTimeout{10000};
};
struct RemoteTransportDiagnostics {
    std::uint64_t received{}, sent{}, retransmitted{}, rejectedEndpoints{}, malformed{}, overflows{};
    Fault fault{};
};
// Socket work belongs exclusively to this transport's worker. The machine lane
// exchanges fixed owned packets; it never calls a socket or waits for a peer.
class RemoteTransport {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    RemoteTransport();
    ~RemoteTransport();
    RemoteTransport(const RemoteTransport&) = delete;
    RemoteTransport& operator=(const RemoteTransport&) = delete;
    [[nodiscard]] bool start(const RemoteTransportConfig&);
    void stop() noexcept;
    [[nodiscard]] std::uint16_t localPort() const noexcept;
    [[nodiscard]] bool send(const Packet&) noexcept; // Exactly one machine producer.
    // Worker publication barrier for a monotonic frame stream. Enqueueing is
    // not transmission; a paused control caller must check this before close.
    [[nodiscard]] bool hasTransmittedFrame(std::uint64_t) const noexcept;
    [[nodiscard]] std::optional<Packet> receive() noexcept; // Same machine consumer.
    [[nodiscard]] Fault fault() const noexcept;
    [[nodiscard]] RemoteTransportDiagnostics diagnostics() const noexcept;
};
}
