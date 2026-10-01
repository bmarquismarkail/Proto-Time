#include "Mailbox.hpp"
#include "machine/modding/TimeModAbi.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <stdexcept>
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace {
struct Plugin {
  Mailbox mailbox;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> generation{0};
  uint64_t observations = 0; // producer-owned
  std::string endpoint; // immutable after construction
  std::thread worker;
  Plugin() {
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR")) {
      const char* display = std::getenv("WAYLAND_DISPLAY");
      if (!display) display = "wayland-0";
      unsigned char digest[SHA256_DIGEST_LENGTH];
      SHA256(reinterpret_cast<const unsigned char*>(display), std::strlen(display), digest);
      constexpr char hex[] = "0123456789abcdef";
      endpoint = std::string(runtime) + "/river-xmb-";
      for (unsigned i = 0; i < 8; ++i) { endpoint += hex[digest[i] >> 4]; endpoint += hex[digest[i] & 15]; }
      endpoint += ".sock";
    }
    worker = std::thread([this] { run(); });
  }
  ~Plugin() { stop.store(true, std::memory_order_release); worker.join(); }
  Json request(Json message) {
    struct Socket { int fd; ~Socket() { if (fd >= 0) ::close(fd); } } socket{
      ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    sockaddr_un address{};
    if (socket.fd < 0 || endpoint.empty() || endpoint.size() >= sizeof(address.sun_path))
      throw std::runtime_error("endpoint unavailable");
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
    const auto deadline = Clock::now() + std::chrono::milliseconds(300);
    auto wait = [&](short events) {
      while (!stop.load(std::memory_order_acquire) && Clock::now() < deadline) {
        pollfd p{socket.fd, events, 0};
        int rc = ::poll(&p, 1, 10);
        if (rc > 0 && (p.revents & events)) return;
        if (rc < 0 && errno == EINTR) continue;
        if (rc != 0) break;
      }
      throw std::runtime_error("IPC interrupted or timed out");
    };
    if (::connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 && errno != EINPROGRESS)
      throw std::runtime_error("connect failed");
    wait(POLLOUT);
    int error = 0; socklen_t size = sizeof(error);
    if (::getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &size) || error)
      throw std::runtime_error("connect failed");
    message["version"] = 1;
    auto bytes = message.dump() + "\n";
    if (bytes.size() > 65536) throw std::runtime_error("request too large");
    for (size_t offset = 0; offset < bytes.size();) {
      wait(POLLOUT);
      auto n = ::send(socket.fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      if (n <= 0) throw std::runtime_error("send failed");
      offset += static_cast<size_t>(n);
    }
    bytes.clear();
    for (;;) {
      wait(POLLIN);
      char buffer[4096];
      auto n = ::recv(socket.fd, buffer, sizeof(buffer), 0);
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
      if (n <= 0) throw std::runtime_error("receive failed");
      bytes.append(buffer, static_cast<size_t>(n));
      if (bytes.size() > 65536) throw std::runtime_error("response too large");
      auto end = bytes.find('\n');
      if (end == std::string::npos) continue;
      auto result = Json::parse(bytes.substr(0, end));
      if (!result.value("ok", false)) throw std::runtime_error("request rejected");
      return result;
    }
  }
  void run() noexcept {
    Snapshot latest{};
    std::string token, app;
    uint64_t published = 0;
    auto retry = Clock::now(), renew = retry;
    auto owned = [&](Json message) { message["owner_token"] = token; return request(std::move(message)); };
    while (!stop.load(std::memory_order_acquire)) {
      Snapshot incoming;
      while (mailbox.pop(incoming)) latest = incoming;
      try {
        auto now = Clock::now();
        if (now >= retry) {
          auto current = generation.load(std::memory_order_acquire);
          if (latest.generation != current) latest = {};
          if (!token.empty() && (latest.app.data() != app || !current)) {
            owned({{"action", "client-remove"}}); token.clear(); published = 0;
          }
          if (latest.app[0] && token.empty()) {
            std::string window, after;
            // Bound pagination even if the server repeats a cursor.
            for (unsigned page = 0; page < 32; ++page) {
              auto listing = request({{"action", "window-list"}, {"after", after}});
              if (!listing["windows"].is_array() || listing["windows"].size() > 32)
                throw std::runtime_error("invalid window list");
              for (const auto& v : listing["windows"]) if (v.value("app_id", "") == latest.app.data()) {
                if (!window.empty()) throw std::runtime_error("ambiguous window");
                window = v.at("window_id").get<std::string>();
              }
              if (listing.at("next").is_null()) break;
              after = listing.at("next").get<std::string>();
              if (page == 31) throw std::runtime_error("too many pages");
            }
            if (window.empty()) throw std::runtime_error("window unavailable");
            token = request({{"action", "client-register"}, {"name", "proto-time-telemetry"}, {"window_id", window}}).at("owner_token").get<std::string>();
            app = latest.app.data(); renew = now; published = 0;
          }
          if (!token.empty() && latest.observations != published && latest.generation == generation.load(std::memory_order_acquire)) {
            owned({{"action", "state-replace"}, {"state", {{"telemetry", {{"rom_sha1", latest.rom.data()}, {"generation", latest.generation}, {"observations", latest.observations}}}}}});
            published = latest.observations; renew = now;
          } else if (!token.empty() && now - renew >= std::chrono::seconds(5)) {
            owned({{"action", "client-renew"}}); renew = now;
          }
        }
      } catch (...) {
        token.clear(); published = 0;
        retry = Clock::now() + std::chrono::seconds(1);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Lease expiry cleans up on shutdown, without delaying machine retirement.
  }
};
int32_t create(const TimeModHostV1* host, void** out) {
  if (!out) return 0;
  *out = nullptr;
  if (!host || host->struct_size < sizeof(*host) || host->abi_version != 1) return 0;
  try { *out = new Plugin; return 1; } catch (...) { return 0; }
}
void destroy(void* p) { delete static_cast<Plugin*>(p); }
int32_t invoke(void*, TimeModCallV1*) { return 1; }
int32_t reset(void* p) { static_cast<Plugin*>(p)->generation.store(0, std::memory_order_release); return 1; }
int32_t save(void*, uint8_t*, uint32_t size) { return size == 0; }
int32_t restore(void* p, const uint8_t*, uint32_t size) { return size == 0 ? reset(p) : 0; }
int32_t observe(void* p, const TimeModObservationV1* o) {
  if (!p || !o || o->struct_size < sizeof(*o) || o->abi_version != 1 || !o->rom_sha1 || !o->frontend_app_id || !o->generation) return 0;
  Snapshot snapshot;
  const auto romSize = strnlen(o->rom_sha1, snapshot.rom.size());
  const auto appSize = strnlen(o->frontend_app_id, snapshot.app.size());
  if (romSize != 40 || appSize == 0 || appSize >= snapshot.app.size()) return 0;
  for (size_t i = 0; i < romSize; ++i) if (!((o->rom_sha1[i] >= '0' && o->rom_sha1[i] <= '9') || (o->rom_sha1[i] >= 'a' && o->rom_sha1[i] <= 'f'))) return 0;
  std::memcpy(snapshot.rom.data(), o->rom_sha1, romSize);
  std::memcpy(snapshot.app.data(), o->frontend_app_id, appSize);
  snapshot.generation = o->generation;
  auto& plugin = *static_cast<Plugin*>(p);
  snapshot.observations = ++plugin.observations;
  plugin.generation.store(o->generation, std::memory_order_release);
  plugin.mailbox.push(snapshot); // saturation drops samples, never waits
  return 1;
}
const TimeModApiV1 api{sizeof(TimeModApiV1), 1, "river-xmb-telemetry", "1.0.0", 1, create, destroy, invoke, reset, 0, save, restore};
const TimeModObserverV1 observer{sizeof(TimeModObserverV1), 1, observe};
}
extern "C" TIME_MOD_EXPORT const TimeModApiV1* time_get_mod_v1() { return &api; }
extern "C" TIME_MOD_EXPORT const TimeModObserverV1* time_get_mod_observer_v1() { return &observer; }
