#include "machine/modding/NativeMod.hpp"
#include "mods/river_xmb/Mailbox.hpp"
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
using Json = nlohmann::json;
using namespace std::chrono_literals;
int main(int argc, char** argv) {
  assert(argc == 2);
  Mailbox queue;
  Snapshot s, copied;
  s.generation = 3;
  assert(queue.push(s) && queue.push(s) && !queue.push(s));
  s.generation = 4;
  assert(queue.pop(copied) && copied.generation == 3);
  assert(queue.pop(copied) && !queue.pop(copied));
  auto root = std::filesystem::temp_directory_path() / ("time-river-test-" + std::to_string(getpid()));
  std::filesystem::create_directory(root);
  setenv("XDG_RUNTIME_DIR", root.c_str(), 1);
  setenv("WAYLAND_DISPLAY", "test", 1);
  unsigned char digest[32]; SHA256(reinterpret_cast<const unsigned char*>("test"), 4, digest);
  constexpr char hex[] = "0123456789abcdef";
  std::string path = root.string() + "/river-xmb-";
  for (unsigned i = 0; i < 8; ++i) { path += hex[digest[i] >> 4]; path += hex[digest[i] & 15]; }
  path += ".sock";
  int server = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  std::copy(path.c_str(), path.c_str() + path.size() + 1, address.sun_path);
  assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  assert(listen(server, 8) == 0);
  std::atomic<bool> stop{false};
  std::atomic<unsigned> states{0};
  std::atomic<uint64_t> observedGeneration{0};
  auto serve = [&] {
    while (!stop.load()) {
      pollfd p{server, POLLIN, 0};
      if (poll(&p, 1, 10) <= 0) continue;
      int fd = accept4(server, nullptr, nullptr, SOCK_NONBLOCK);
      if (fd < 0) continue;
      std::string bytes;
      auto deadline = std::chrono::steady_clock::now() + 500ms;
      while (!stop.load() && bytes.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        pollfd client{fd, POLLIN, 0};
        if (poll(&client, 1, 10) <= 0) continue;
        char buffer[4096]; auto n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        bytes.append(buffer, n);
      }
      if (bytes.find('\n') != std::string::npos) {
        auto request = Json::parse(bytes);
        assert(request["version"] == 1);
        Json response{{"ok", true}};
        auto action = request.at("action").get<std::string>();
        if (action == "window-list") {
          response["windows"] = Json::array({{{"window_id", "opaque-window"}, {"app_id", "fixture.app"}}});
          response["next"] = nullptr;
        } else if (action == "client-register") {
          assert(request["window_id"] == "opaque-window"); response["owner_token"] = "private-token";
        } else {
          assert(request["owner_token"] == "private-token");
          if (action == "state-replace") {
            auto telemetry = request["state"]["telemetry"];
            assert(telemetry["rom_sha1"] == std::string(40, 'a'));
            observedGeneration.store(telemetry["generation"].get<uint64_t>());
            states.fetch_add(1);
          }
        }
        auto result = response.dump() + "\n";
        assert(send(fd, result.data(), result.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(result.size()));
      }
      close(fd);
    }
  };
  std::thread peer(serve);
  using namespace BMMQ::Modding;
  auto host = std::make_shared<ModHost>();
  LoadedMod metadata{"river-xmb-telemetry", "1.0.0", 0, {}, argv[1], {}};
  auto module = NativeMod::load(metadata, host);
  std::string identity(40, 'a'), app = "fixture.app";
  TimeModObservationV1 observation{sizeof(observation), 1, 7, identity.c_str(), app.c_str(), nullptr, nullptr};
  assert(module->observe(observation));
  // Mutate borrowed buffers immediately: the worker must use owned values.
  identity.assign(40, 'b'); app = "changed";
  auto waitFor = [&](uint64_t generation) {
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (observedGeneration.load() != generation && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(10ms);
    assert(observedGeneration.load() == generation);
  };
  waitFor(7);
  auto count = states.load(); std::this_thread::sleep_for(50ms); assert(states.load() == count);
  identity.assign(40, 'a'); app = "fixture.app";
  observation.rom_sha1 = identity.c_str(); observation.frontend_app_id = app.c_str(); observation.generation = 8;
  assert(module->observe(observation)); waitFor(8);
  observation.struct_size = 0; assert(!module->observe(observation)); observation.struct_size = sizeof(observation);
  module.reset();
  stop.store(true); peer.join(); close(server); std::filesystem::remove(path);
  // Endpoint unavailable and queue saturation cannot block the producer or retirement.
  module = NativeMod::load(metadata, host);
  auto start = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < 10000; ++i) assert(module->observe(observation));
  assert(std::chrono::steady_clock::now() - start < 1s);
  // Recover when an endpoint appears after the initial failed connection.
  std::this_thread::sleep_for(50ms);
  server = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  assert(listen(server, 8) == 0);
  stop.store(false); observedGeneration.store(0);
  peer = std::thread(serve);
  assert(module->observe(observation)); waitFor(8);
  start = std::chrono::steady_clock::now(); module.reset();
  assert(std::chrono::steady_clock::now() - start < 500ms);
  stop.store(true); peer.join();
  // A peer that accepts but never replies must not hold module retirement.
  module = NativeMod::load(metadata, host);
  assert(module->observe(observation));
  pollfd waiting{server, POLLIN, 0};
  assert(poll(&waiting, 1, 1000) > 0);
  int stalled = accept4(server, nullptr, nullptr, SOCK_NONBLOCK);
  assert(stalled >= 0);
  std::this_thread::sleep_for(20ms);
  start = std::chrono::steady_clock::now(); module.reset();
  assert(std::chrono::steady_clock::now() - start < 500ms);
  close(stalled); close(server);
  std::filesystem::remove_all(root);
}
