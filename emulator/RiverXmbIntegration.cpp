#include "emulator/RiverXmbIntegration.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>

namespace BMMQ {
using json = nlohmann::json;

RiverXmbIntegration::RiverXmbIntegration(std::size_t capacity) : capacity_(capacity) { thread_ = std::thread(&RiverXmbIntegration::worker, this); }
RiverXmbIntegration::~RiverXmbIntegration() { stop(); }
void RiverXmbIntegration::stop() { { std::lock_guard lock(mutex_); if (stopping_) return; stopping_ = true; } ready_.notify_one(); if (thread_.joinable()) thread_.join(); }

std::optional<std::string> RiverXmbIntegration::socketPath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime) return std::nullopt;
    const char* display = std::getenv("WAYLAND_DISPLAY");
    const std::string value = display ? display : "wayland-0";
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
    std::string hex; for (unsigned char byte : digest) { char b[3]; std::snprintf(b, sizeof b, "%02x", byte); hex += b; }
    return (std::filesystem::path(runtime) / ("river-xmb-" + hex.substr(0, 16) + ".sock")).string();
}

std::string RiverXmbIntegration::contextJson(const RiverXmbContext& c, std::string_view action) {
    return json{{"action", action}, {"context", {{"emulator", c.emulator}, {"platform", c.platform}, {"game", c.game}, {"title", c.title}, {"artwork_key", c.artworkKey}, {"visualizer", c.visualizer}, {"visualizer_color", c.visualizerColor}}}}.dump() + "\n";
}
std::string RiverXmbIntegration::telemetryJson(const RiverXmbTelemetry& t) {
    return json{{"action", "telemetry-update"}, {"telemetry", {{"location", t.location}, {"play_time", t.playTime}, {"badges", t.badges}, {"party", json::array({{{"name", "Bulbasaur"}, {"level", 5}, {"hp", t.hp}, {"max_hp", t.maxHp}}})}}}}.dump() + "\n";
}
void RiverXmbIntegration::contextSet(const RiverXmbContext& c) { enqueue(contextJson(c, "context-set"), false); }
void RiverXmbIntegration::contextUpdate(const RiverXmbContext& c) { enqueue(contextJson(c, "context-update"), false); }
void RiverXmbIntegration::contextClear() { enqueue(json{{"action", "context-clear"}}.dump() + "\n", false); }
void RiverXmbIntegration::telemetryUpdate(const RiverXmbTelemetry& t) { auto m = telemetryJson(t); std::lock_guard lock(mutex_); if (m == lastTelemetry_) return; lastTelemetry_ = m; if (queue_.size() >= capacity_) { for (auto it = queue_.begin(); it != queue_.end(); ++it) if (it->find("telemetry-update") != std::string::npos) { queue_.erase(it); break; } } if (queue_.size() >= capacity_) queue_.pop_front(); queue_.push_back(std::move(m)); ready_.notify_one(); }
void RiverXmbIntegration::enqueue(std::string message, bool) { std::lock_guard lock(mutex_); if (stopping_) return; if (queue_.size() >= capacity_) queue_.pop_front(); queue_.push_back(std::move(message)); ready_.notify_one(); }
void RiverXmbIntegration::worker() { for (;;) { std::string message; { std::unique_lock lock(mutex_); ready_.wait(lock, [&]{ return stopping_ || !queue_.empty(); }); if (queue_.empty() && stopping_) return; message = std::move(queue_.front()); queue_.pop_front(); } auto path = socketPath(); if (!path) continue; int fd = ::socket(AF_UNIX, SOCK_STREAM, 0); if (fd < 0) { std::cerr << "warning: River XMB IPC socket creation failed\n"; continue; } sockaddr_un address{}; address.sun_family = AF_UNIX; std::strncpy(address.sun_path, path->c_str(), sizeof(address.sun_path)-1); if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) { if (::send(fd, message.data(), message.size(), MSG_NOSIGNAL) < 0) std::cerr << "warning: River XMB IPC send failed\n"; } else if (errno != ENOENT && errno != ECONNREFUSED) { std::cerr << "warning: River XMB IPC connect failed: " << std::strerror(errno) << "\n"; } ::close(fd); } }
} // namespace BMMQ
