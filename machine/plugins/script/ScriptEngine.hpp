#pragma once
#include "machine/plugins/debug/DebugService.hpp"
#include <chrono>
#include <string>
#include <string_view>
namespace BMMQ::Script {
enum class Language { File, Lua, Python, JavaScript };
struct Permissions {
  bool registers{}, ram{}, reports{true};
};
struct Limits {
  std::size_t sourceBytes{65536}, heapBytes{8 * 1024 * 1024};
  std::uint64_t instructions{1'000'000};
  std::chrono::milliseconds timeout{1000};
};
struct Snapshot {
  std::string core;
  Debug::Reply state;
  std::uint16_t memoryBase{};
};
struct Result {
  bool success{}, permissionDenied{}, runtimeUnavailable{};
  Debug::Command staged{};
  std::string report{}, error{};
};
// No Machine reference is accepted. Every interpreter sees only an owned copy;
// success returns a bounded mutation recipe for machine-lane validation.
class ScriptEngine {
public:
  static bool available(Language) noexcept;
  static Result evaluate(Language, std::string_view, Snapshot, Permissions = {},
                         Limits = {});
};
// Shared invocation bindings used by runtime adapters, never by guest
// callbacks.
struct Invocation {
  const Snapshot &snapshot;
  Permissions permissions;
  Limits limits;
  Debug::Command staged{};
  std::string report;
  std::uint64_t work{};
  bool failed{}, accepting{true}, permissionDenied{};
  std::chrono::steady_clock::time_point deadline;
  Invocation(const Snapshot &, Permissions, Limits);
  bool budget(std::uint64_t count = 1) noexcept;
  bool getRegister(std::string_view, std::uint16_t &) const noexcept;
  bool setRegister(std::string_view, std::uint16_t) noexcept;
  bool getByte(std::uint16_t, std::uint8_t &) const noexcept;
  bool setByte(std::uint16_t, std::uint8_t) noexcept;
  bool output(std::string_view);
};
Result evaluateLua(std::string_view, Invocation &);
Result evaluatePython(std::string_view, Invocation &);
Result evaluateJavaScript(std::string_view, Invocation &);
} // namespace BMMQ::Script
