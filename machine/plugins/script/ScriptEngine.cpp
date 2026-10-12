#include "ScriptEngine.hpp"
#include "space/CoreModel.hpp"
#include "space/Project.hpp"
#include <limits>
#include <optional>
#ifdef TIME_SCRIPT_PYTHON
#include <unistd.h>
#endif
namespace BMMQ::Script {
Invocation::Invocation(const Snapshot &view, Permissions allowed, Limits budget)
    : snapshot(view), permissions(allowed), limits(budget),
      deadline(std::chrono::steady_clock::now() + budget.timeout) {
  staged.operation = Debug::Operation::Edit;
  staged.generation = view.state.generation;
  staged.pauseId = view.state.pauseId;
  staged.registers = view.state.registers;
}
bool Invocation::budget(std::uint64_t count) noexcept {
  if (count > limits.instructions - work) {
    failed = true;
    return false;
  }
  work += count;
  if (std::chrono::steady_clock::now() > deadline) {
    failed = true;
    return false;
  }
  return !failed;
}
bool Invocation::getRegister(std::string_view name,
                             std::uint16_t &value) const noexcept {
  const auto &model = Space::coreModel(snapshot.core);
  for (std::size_t i = 0; i < model.pairs.size(); ++i)
    if (name == model.pairs[i]) {
      value = staged.registers[i];
      return true;
    }
  return false;
}
bool Invocation::setRegister(std::string_view name,
                             std::uint16_t value) noexcept {
  if (!accepting || !permissions.registers) {
    permissionDenied |= !permissions.registers;
    failed = true;
    return false;
  }
  const auto &model = Space::coreModel(snapshot.core);
  for (std::size_t i = 0; i < model.pairs.size(); ++i)
    if (name == model.pairs[i]) {
      if (value > model.registerMaximum(i) ||
          (snapshot.core == "gameboy" && i == 0 && (value & 15))) {
        failed = true;
        return false;
      }
      staged.registers[i] = value;
      staged.editRegisters = true;
      return true;
    }
  failed = true;
  return false;
}
bool Invocation::getByte(std::uint16_t address,
                         std::uint8_t &value) const noexcept {
  for (unsigned i = staged.byteCount; i > 0; --i)
    if (staged.bytes[i - 1].address == address) {
      value = staged.bytes[i - 1].value;
      return true;
    }
  if (address < snapshot.memoryBase ||
      std::uint32_t(address) - snapshot.memoryBase >= snapshot.state.length)
    return false;
  value = snapshot.state.memory[address - snapshot.memoryBase];
  return true;
}
bool Invocation::setByte(std::uint16_t address, std::uint8_t value) noexcept {
  if (!accepting || !permissions.ram ||
      staged.byteCount == staged.bytes.size()) {
    permissionDenied |= !permissions.ram;
    failed = true;
    return false;
  }
  // Authoritative address/capability validation occurs before machine commit.
  staged.bytes[staged.byteCount++] = {address, value};
  return true;
}
bool Invocation::output(std::string_view text) {
  if (!accepting || !permissions.reports ||
      text.size() > 65536 - report.size()) {
    permissionDenied |= !permissions.reports;
    failed = true;
    return false;
  }
  report.append(text);
  return true;
}
bool ScriptEngine::available(Language language) noexcept {
  switch (language) {
  case Language::File:
    return true;
  case Language::Lua:
#ifdef TIME_SCRIPT_LUA
    return true;
#else
    return false;
#endif
  case Language::Python:
#ifdef TIME_SCRIPT_PYTHON
    return ::access(TIME_SCRIPT_PYTHON_EXECUTABLE, X_OK) == 0 &&
           ::access(TIME_SCRIPT_PYTHON_WORKER, R_OK) == 0;
#else
    return false;
#endif
  case Language::JavaScript:
#ifdef TIME_SCRIPT_QUICKJS
    return true;
#else
    return false;
#endif
  }
  return false;
}
Result ScriptEngine::evaluate(Language language, std::string_view source,
                              Snapshot snapshot, Permissions permissions,
                              Limits limits) {
  std::optional<Invocation> invocationState;
  try {
    (void)Space::coreModel(snapshot.core);
    if (snapshot.state.length > snapshot.state.memory.size() ||
        std::uint32_t(snapshot.memoryBase) + snapshot.state.length > 65536)
      throw std::invalid_argument("invalid owned memory snapshot");
    if (snapshot.state.state != Debug::State::Paused)
      throw std::invalid_argument("scripting requires a paused owned snapshot");
    if (!limits.sourceBytes || limits.sourceBytes > 1024 * 1024 ||
        source.size() > limits.sourceBytes ||
        source.find('\0') != std::string_view::npos ||
        limits.heapBytes < 65536 || limits.heapBytes > 64 * 1024 * 1024 ||
        !limits.instructions || limits.instructions > 100'000'000 ||
        limits.timeout.count() < 1 || limits.timeout.count() > 30000)
      throw std::invalid_argument("script resource limits rejected");
    if (!available(language))
      return {.runtimeUnavailable = true,
              .error = "script runtime unavailable"};
    invocationState.emplace(snapshot, permissions, limits);
    auto &invocation = *invocationState;
    Result result;
    if (language == Language::File) {
      if (source.size() > limits.heapBytes / 32)
        throw std::invalid_argument(
            "file automation heap/source budget rejected");
      const auto data = Space::Json::parse(
          source, [](int depth, Space::Json::parse_event_t, Space::Json &) {
            if (depth > 32)
              throw std::invalid_argument(
                  "file automation nesting budget exhausted");
            return true;
          });
      if (data.at("schemaVersion") != 1 || data.at("core") != snapshot.core ||
          !data.at("actions").is_array() || data["actions"].size() > 128)
        throw std::invalid_argument(
            "file automation core/schema/action budget mismatch");
      for (const auto &action : data["actions"]) {
        if (!invocation.budget())
          throw std::invalid_argument("script budget exhausted");
        const auto operation = action.at("operation").get<std::string>();
        if (operation == "register") {
          if (!action.at("value").is_number_integer() || action["value"] < 0 ||
              action["value"] > 65535 ||
              !invocation.setRegister(action.at("name").get<std::string>(),
                                      action["value"].get<std::uint16_t>()))
            throw std::invalid_argument("register mutation denied or invalid");
        } else if (operation == "write") {
          if (!action.at("address").is_number_integer() ||
              action["address"] < 0 || action["address"] > 65535 ||
              !action.at("value").is_number_integer() || action["value"] < 0 ||
              action["value"] > 255 ||
              !invocation.setByte(action["address"].get<std::uint16_t>(),
                                  action["value"].get<std::uint8_t>()))
            throw std::invalid_argument("RAM mutation denied or invalid");
        } else if (operation == "report") {
          if (!invocation.output(action.at("text").get<std::string>()))
            throw std::invalid_argument("report permission/budget rejected");
        } else
          throw std::invalid_argument("unsupported file automation operation");
      }
      result.success = true;
    } else if (language == Language::Lua)
      result = evaluateLua(source, invocation);
    else if (language == Language::Python)
      result = evaluatePython(source, invocation);
    else
      result = evaluateJavaScript(source, invocation);
    result.permissionDenied |= invocation.permissionDenied;
    if (!result.success)
      return result;
    if (!invocation.budget(0))
      throw std::invalid_argument("script budget exhausted");
    result.staged = invocation.staged;
    result.report = std::move(invocation.report);
    return result;
  } catch (const std::exception &error) {
    return {.permissionDenied =
                invocationState && invocationState->permissionDenied,
            .error = error.what()};
  }
}
} // namespace BMMQ::Script
