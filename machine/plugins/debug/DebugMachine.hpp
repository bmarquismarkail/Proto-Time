#pragma once
#include "DebugEngine.hpp"
#include <string_view>
namespace BMMQ::Debug {
// Internal interface, separately versioned from the existing external plugin
// ABIs.
class IDebugMachineV1 {
public:
  virtual ~IDebugMachineV1() = default;
  virtual bool debugPortBus() const noexcept { return false; }
  virtual void connectDebugEngine(DebugEngine *) = 0;
  virtual std::span<const char *const> debugRegisterNames() const noexcept = 0;
  virtual std::array<std::uint16_t, 20> debugRegisters() const = 0;
  virtual bool debugValidateRegisters(
      const std::array<std::uint16_t, 20> &) const noexcept = 0;
  virtual void
  debugCommitRegisters(const std::array<std::uint16_t, 20> &) noexcept = 0;
  virtual bool debugPeek(std::uint16_t, std::uint8_t &) const noexcept = 0;
  virtual bool debugWritable(std::uint16_t) const noexcept = 0;
  virtual void debugCommitByte(std::uint16_t, std::uint8_t) noexcept = 0;
  virtual std::uint64_t debugBacking(std::uint16_t) const noexcept = 0;
  virtual void debugEdited() noexcept = 0;
};
} // namespace BMMQ::Debug
