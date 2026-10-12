#pragma once
#include "../DebugService.hpp"
#include "space/Project.hpp"
#include <map>
#include <string>
#include <vector>
namespace BMMQ::Debug {
class DapAdapter {
  std::string core_;
  std::string romHash_;
  std::vector<std::string> names_;
  Reply current_{};
  bool initialized_{}, attached_{};
  std::uint32_t sequence_{1};
  struct Pending {
    Space::Json request;
    Command command;
  };
  std::map<std::uint32_t, Pending> pending_;
  std::array<Breakpoint, 64> breaks_{};
  std::array<Watchpoint, 64> watches_{};
  std::uint16_t breakCount_{}, watchCount_{};
  int variablesRef_{};
  std::map<std::uint64_t, Space::Json> symbols_;
  Space::Json message(const Space::Json &, bool,
                      Space::Json = Space::Json::object(), std::string = {});
  Space::Json event(std::string, Space::Json);

public:
  DapAdapter(std::string core, const Reply &initial, std::string romHash = {});
  void symbols(const Space::Json &validatedProject);
  // All parsing, allocation and formatting belongs to the transport lane.
  // Immediate messages are ready for wire framing; commands contain only
  // values.
  std::optional<Command> prepare(const Space::Json &,
                                 std::vector<Space::Json> &messages);
  std::vector<Space::Json> complete(const Reply &);
  Space::Json reject(const Space::Json &, std::string);
  const Reply &current() const noexcept { return current_; }
  static std::string frame(const Space::Json &);
};
// Bounded incremental DAP framing, including fragmented and coalesced messages.
class DapFramer {
  std::string buffer_;

public:
  std::vector<Space::Json> feed(std::string_view);
  bool incomplete() const noexcept { return !buffer_.empty(); }
};
} // namespace BMMQ::Debug
