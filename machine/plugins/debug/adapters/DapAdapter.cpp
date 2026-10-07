#include "DapAdapter.hpp"
#include "space/CoreModel.hpp"
#include <charconv>
#include <iomanip>
#include <sstream>
namespace BMMQ::Debug {
using Json = Space::Json;
namespace {
std::uint64_t number(const Json &value, std::uint64_t limit = 65535) {
  std::uint64_t result = 0;
  if (value.is_number_unsigned())
    result = value.get<std::uint64_t>();
  else if (value.is_number_integer()) {
    auto signedValue = value.get<std::int64_t>();
    if (signedValue < 0)
      throw std::invalid_argument("negative value");
    result = signedValue;
  } else if (value.is_string()) {
    auto s = value.get<std::string>();
    int base = 10;
    if (s.starts_with("0x")) {
      s.erase(0, 2);
      base = 16;
    }
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), result, base);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
      throw std::invalid_argument("invalid integer");
  } else
    throw std::invalid_argument("integer required");
  if (result > limit)
    throw std::invalid_argument("value exceeds supported range");
  return result;
}
std::string hex(std::uint16_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setw(4) << std::setfill('0') << value;
  return out.str();
}
std::uint16_t offsetAddress(const Json &args, const char *key) {
  auto offset = args.value("offset", std::int64_t(0));
  if (offset < -65535 || offset > 65535)
    throw std::invalid_argument("offset outside address space");
  auto address = std::int64_t(number(args.at(key))) + offset;
  if (address < 0 || address > 65535)
    throw std::invalid_argument("address outside address space");
  return std::uint16_t(address);
}
std::string encode(std::span<const std::uint8_t> bytes) {
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  for (size_t i = 0; i < bytes.size(); i += 3) {
    auto n = std::uint32_t(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      n |= std::uint32_t(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      n |= bytes[i + 2];
    result += alphabet[n >> 18];
    result += alphabet[(n >> 12) & 63];
    result += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
    result += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
  }
  return result;
}
std::vector<std::uint8_t> decode(const std::string &text) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (text.size() > 88 || text.size() % 4)
    throw std::invalid_argument("writeMemory accepts at most 64 bytes");
  std::vector<std::uint8_t> bytes;
  for (size_t i = 0; i < text.size(); i += 4) {
    std::uint32_t n = 0;
    unsigned padding = 0;
    for (unsigned j = 0; j < 4; ++j) {
      auto c = text[i + j];
      auto pos = alphabet.find(c);
      if (c == '=') {
        if (j < 2 || i + 4 != text.size())
          throw std::invalid_argument("invalid base64 padding");
        ++padding;
        pos = 0;
      } else if (pos == std::string_view::npos || padding)
        throw std::invalid_argument("invalid base64");
      n = (n << 6) | pos;
    }
    if (padding > 2 || (padding == 2 && (n & 0xffff)) ||
        (padding == 1 && (n & 0xff)))
      throw std::invalid_argument("noncanonical base64");
    bytes.push_back(n >> 16);
    if (padding < 2)
      bytes.push_back(n >> 8);
    if (!padding)
      bytes.push_back(n);
  }
  if (bytes.size() > 64)
    throw std::invalid_argument("writeMemory accepts at most 64 bytes");
  return bytes;
}
const char *reason(Reason value) {
  switch (value) {
  case Reason::Step:
    return "step";
  case Reason::Breakpoint:
    return "breakpoint";
  case Reason::Watchpoint:
    return "data breakpoint";
  case Reason::Entry:
    return "entry";
  case Reason::Fault:
    return "exception";
  default:
    return "pause";
  }
}
} // namespace
DapAdapter::DapAdapter(std::string core, const Reply &initial,
                       std::string romHash)
    : core_(std::move(core)), romHash_(std::move(romHash)), current_(initial) {
  for (auto name : Space::coreModel(core_).pairs)
    names_.emplace_back(name);
}
void DapAdapter::symbols(const Json &project) {
  Space::Project::validate(project);
  if (project.at("core") != core_ || romHash_.empty() ||
      project.at("romSha256") != romHash_)
    throw std::invalid_argument("debugger symbol core/ROM mismatch");
  auto next = symbols_;
  if (project.contains("symbolImports"))
    for (const auto &import : project["symbolImports"])
      for (const auto &symbol : import["symbols"])
        next[Space::counter(symbol["location"])] = symbol;
  symbols_ = std::move(next);
}
Json DapAdapter::message(const Json &request, bool success, Json body,
                         std::string error) {
  Json result = {{"seq", sequence_++},
                 {"type", "response"},
                 {"request_seq", request.at("seq")},
                 {"command", request.at("command")},
                 {"success", success}};
  if (success)
    result["body"] = std::move(body);
  else
    result["message"] = std::move(error);
  return result;
}
Json DapAdapter::event(std::string name, Json body) {
  return {{"seq", sequence_++},
          {"type", "event"},
          {"event", std::move(name)},
          {"body", std::move(body)}};
}
Json DapAdapter::reject(const Json &request, std::string error) {
  return message(request, false, {}, std::move(error));
}
std::optional<Command> DapAdapter::prepare(const Json &request,
                                           std::vector<Json> &messages) {
  try {
    if (request.at("type") != "request" ||
        !request.at("seq").is_number_integer())
      throw std::invalid_argument("DAP request required");
    auto id = number(request.at("seq"), 0x7fffffff);
    if (!id || pending_.contains(id) || pending_.size() >= 32)
      throw std::invalid_argument("request identity/budget rejected");
    auto name = request.at("command").get<std::string>();
    const auto args = request.value("arguments", Json::object());
    if (name == "initialize") {
      if (initialized_)
        throw std::invalid_argument("already initialized");
      initialized_ = true;
      messages.push_back(message(request, true,
                                 {{"supportsConfigurationDoneRequest", true},
                                  {"supportsInstructionBreakpoints", true},
                                  {"supportsDataBreakpoints", true},
                                  {"supportsReadMemoryRequest", true},
                                  {"supportsWriteMemoryRequest", true},
                                  {"supportsSetVariable", true},
                                  {"supportsSteppingGranularity", true}}));
      return {};
    }
    if (!initialized_)
      throw std::invalid_argument("initialize first");
    if (name == "attach" || name == "launch") {
      if (attached_)
        throw std::invalid_argument("already attached");
      attached_ = true;
      messages.push_back(event("initialized", {}));
      // Attach response is deferred until configurationDone.
      Command command{.id = std::uint32_t(id),
                      .operation = Operation::Inspect,
                      .generation = current_.generation,
                      .pauseId = current_.pauseId};
      pending_.emplace(id, Pending{request, command});
      return {};
    }
    if (!attached_)
      throw std::invalid_argument("attach or launch first");
    if (name == "configurationDone") {
      messages.push_back(message(request, true));
      for (auto it = pending_.begin(); it != pending_.end();) {
        auto n = it->second.request.at("command");
        if (n == "attach" || n == "launch") {
          messages.push_back(message(it->second.request, true));
          it = pending_.erase(it);
        } else
          ++it;
      }
      messages.push_back(event(
          "stopped",
          {{"reason", "entry"}, {"threadId", 1}, {"allThreadsStopped", true}}));
      return {};
    }
    if (name == "threads") {
      messages.push_back(message(
          request, true,
          {{"threads", Json::array({{{"id", 1}, {"name", core_ + " CPU"}}})}}));
      return {};
    }
    if (args.contains("threadId") && args["threadId"] != 1)
      throw std::invalid_argument("unknown thread");
    Command command{.id = std::uint32_t(id),
                    .generation = current_.generation,
                    .pauseId = current_.pauseId};
    for (const auto &[pendingId, pending] : pending_) {
      (void)pendingId;
      const auto pendingName = pending.request.at("command");
      if (pendingName != "attach" && pendingName != "launch")
        throw std::invalid_argument(
            "machine request pending; retry after response");
    }
    if (name == "continue")
      command.operation = Operation::Continue;
    else if (name == "pause")
      command.operation = Operation::Pause;
    else if (name == "stepIn" || name == "next") {
      if (args.value("granularity", std::string("instruction")) !=
          "instruction")
        throw std::invalid_argument("instruction stepping required");
      command.operation = Operation::Step;
    } else if (name == "disconnect")
      command.operation = Operation::Disconnect;
    else if (name == "setInstructionBreakpoints" ||
             name == "setDataBreakpoints") {
      auto rules = args.at("breakpoints");
      if (!rules.is_array() || rules.size() > 64)
        throw std::invalid_argument("breakpoint budget exceeded");
      command.operation = Operation::Rules;
      command.breaks = breaks_;
      command.watches = watches_;
      command.breakCount = breakCount_;
      command.watchCount = watchCount_;
      if (name == "setInstructionBreakpoints") {
        command.breakCount = rules.size();
        unsigned i = 0;
        for (auto &rule : rules) {
          if (rule.contains("condition") || rule.contains("hitCondition"))
            throw std::invalid_argument("conditional breakpoints unsupported");
          auto address = offsetAddress(rule, "instructionReference");
          command.breaks[i++] = {.address = std::uint16_t(address)};
        }
      } else {
        command.watchCount = rules.size();
        unsigned i = 0;
        for (auto &rule : rules) {
          auto data = rule.at("dataId").get<std::string>();
          auto split = data.find(':');
          if (split == std::string::npos)
            throw std::invalid_argument("invalid data breakpoint");
          const auto access = rule.value("accessType", std::string("write"));
          bool port = data.substr(0, split) == "port";
          if (port && core_ == "gameboy")
            throw std::invalid_argument("core has no port bus");
          if (rule.contains("condition") || rule.contains("hitCondition"))
            throw std::invalid_argument("conditional watchpoints unsupported");
          if (!port && data.substr(0, split) != "memory")
            throw std::invalid_argument("invalid data space");
          if (access != "read" && access != "write")
            throw std::invalid_argument(
                "readWrite watches require separate read and write rules");
          auto address = number(data.substr(split + 1));
          command.watches[i++] = {
              std::uint16_t(address), std::uint16_t(address),
              port ? (access == "read" ? Access::PortRead : Access::PortWrite)
                   : (access == "read" ? Access::Read : Access::Write)};
        }
      }
    } else if (name == "readMemory") {
      command.operation = Operation::Inspect;
      command.address = offsetAddress(args, "memoryReference");
      command.length = number(args.at("count"), 256);
    } else if (name == "writeMemory") {
      command.operation = Operation::Edit;
      auto address = offsetAddress(args, "memoryReference");
      auto bytes = decode(args.at("data").get<std::string>());
      if (address + std::int64_t(bytes.size()) > 65536)
        throw std::invalid_argument("memory address out of range");
      command.byteCount = bytes.size();
      for (unsigned i = 0; i < bytes.size(); ++i)
        command.bytes[i] = {std::uint16_t(address + i), bytes[i]};
    } else if (name == "setVariable") {
      if (current_.state != State::Paused || !variablesRef_ ||
          args.at("variablesReference") != variablesRef_)
        throw std::invalid_argument("stale register scope");
      auto found = std::find(names_.begin(), names_.end(),
                             args.at("name").get<std::string>());
      if (found == names_.end())
        throw std::invalid_argument("unknown register");
      command.operation = Operation::Edit;
      command.editRegisters = true;
      command.registers = current_.registers;
      command.registers[found - names_.begin()] = number(args.at("value"));
    } else if (name == "stackTrace" || name == "scopes" ||
               name == "variables" || name == "evaluate" ||
               name == "dataBreakpointInfo") {
      if (current_.state != State::Paused)
        throw std::invalid_argument("inspection requires pause");
      Json body;
      if (name == "stackTrace") {
        Json frame = {
            {"id", current_.pauseId},
            {"name", hex(current_.registers[5])},
            {"line", 0},
            {"column", 0},
            {"instructionPointerReference", hex(current_.registers[5])}};
        if (auto symbol = symbols_.find(current_.codeBacking);
            symbol != symbols_.end()) {
          frame["name"] = symbol->second["name"];
          if (symbol->second.contains("source")) {
            frame["source"] = {{"path", symbol->second["source"]}};
            frame["line"] = symbol->second["line"];
            frame["column"] = 1;
          }
        }
        body = {{"stackFrames", Json::array({frame})}, {"totalFrames", 1}};
      } else if (name == "scopes") {
        if (args.at("frameId") != current_.pauseId)
          throw std::invalid_argument("stale frame");
        variablesRef_ = int(current_.pauseId % 0x3ffffffe) + 1;
        body = {{"scopes", Json::array({{{"name", "Registers"},
                                         {"variablesReference", variablesRef_},
                                         {"expensive", false}}})}};
      } else if (name == "variables") {
        if (!variablesRef_ || args.at("variablesReference") != variablesRef_)
          throw std::invalid_argument("stale scope");
        body["variables"] = Json::array();
        for (unsigned i = 0; i < names_.size(); ++i)
          body["variables"].push_back({{"name", names_[i]},
                                       {"value", hex(current_.registers[i])},
                                       {"type", "uint16"},
                                       {"variablesReference", 0}});
      } else if (name == "evaluate") {
        auto found = std::find(names_.begin(), names_.end(),
                               args.at("expression").get<std::string>());
        if (found == names_.end())
          throw std::invalid_argument(
              "only architectural register names can be evaluated");
        body = {{"result", hex(current_.registers[found - names_.begin()])},
                {"variablesReference", 0}};
      } else {
        auto data = args.at("name").get<std::string>();
        auto split = data.find(':');
        if (split == std::string::npos || (data.substr(0, split) != "memory" &&
                                           data.substr(0, split) != "port"))
          throw std::invalid_argument("use memory:ADDRESS or port:ADDRESS");
        number(data.substr(split + 1));
        if (core_ == "gameboy" && data.starts_with("port:"))
          throw std::invalid_argument("core has no port bus");
        body = {{"dataId", data},
                {"description", data},
                {"accessTypes", Json::array({"read", "write"})},
                {"canPersist", true}};
      }
      messages.push_back(message(request, true, body));
      return {};
    } else
      throw std::invalid_argument("unsupported DAP request");
    pending_.emplace(id, Pending{request, command});
    return command;
  } catch (const std::exception &error) {
    messages.push_back(reject(request, error.what()));
    return {};
  }
}
std::vector<Json> DapAdapter::complete(const Reply &reply) {
  std::vector<Json> messages;
  const bool changed = reply.pauseId != current_.pauseId ||
                       reply.state != current_.state ||
                       reply.generation != current_.generation;
  if (changed)
    variablesRef_ = 0;
  current_ = reply;
  if (reply.id) {
    auto it = pending_.find(reply.id);
    if (it == pending_.end())
      return messages;
    const auto request = it->second.request;
    const auto command = it->second.command;
    const auto name = request.at("command").get<std::string>();
    pending_.erase(it);
    if (reply.error != Error::None) {
      messages.push_back(
          reject(request, "debug request rejected (" +
                              std::to_string(unsigned(reply.error)) + ")"));
      if (changed && attached_ &&
          (reply.state == State::Paused || reply.state == State::Faulted))
        messages.push_back(event("stopped", {{"reason", reason(reply.reason)},
                                             {"threadId", 1},
                                             {"allThreadsStopped", true}}));
      return messages;
    }
    Json body = Json::object();
    if (command.operation == Operation::Rules) {
      breaks_ = command.breaks;
      watches_ = command.watches;
      breakCount_ = command.breakCount;
      watchCount_ = command.watchCount;
      body["breakpoints"] = Json::array();
      for (auto &rule : request["arguments"]["breakpoints"]) {
        (void)rule;
        body["breakpoints"].push_back({{"verified", true}});
      }
    } else if (name == "readMemory")
      body = {{"address", hex(command.address)},
              {"data", encode(std::span(reply.memory.data(), reply.length))}};
    else if (name == "writeMemory")
      body = {{"bytesWritten", command.byteCount}};
    else if (name == "continue")
      body = {{"allThreadsContinued", true}};
    else if (name == "setVariable")
      body = {{"value", request["arguments"]["value"]},
              {"variablesReference", 0}};
    messages.push_back(message(request, true, body));
    if (command.operation == Operation::Disconnect) {
      attached_ = false;
      messages.push_back(event("terminated", {}));
    }
  }
  if (changed && attached_) {
    if (reply.state == State::Running)
      messages.push_back(
          event("continued", {{"threadId", 1}, {"allThreadsContinued", true}}));
    else if (reply.state == State::Paused || reply.state == State::Faulted)
      messages.push_back(event("stopped", {{"reason", reason(reply.reason)},
                                           {"threadId", 1},
                                           {"allThreadsStopped", true}}));
  }
  return messages;
}
std::string DapAdapter::frame(const Json &message) {
  auto body = message.dump();
  return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
std::vector<Json> DapFramer::feed(std::string_view bytes) {
  if (buffer_.size() + bytes.size() > 1024 * 1024)
    throw std::invalid_argument("DAP frame budget exceeded");
  buffer_.append(bytes);
  std::vector<Json> result;
  while (true) {
    auto end = buffer_.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (buffer_.size() > 128)
        throw std::invalid_argument("DAP header budget exceeded");
      break;
    }
    constexpr std::string_view prefix = "Content-Length: ";
    if (!buffer_.starts_with(prefix) || end > 128)
      throw std::invalid_argument("invalid DAP header");
    auto count = number(buffer_.substr(prefix.size(), end - prefix.size()),
                        1024 * 1024 - 132);
    if (!count)
      throw std::invalid_argument("empty DAP frame");
    if (buffer_.size() < end + 4 + count)
      break;
    result.push_back(Json::parse(buffer_.substr(end + 4, count)));
    buffer_.erase(0, end + 4 + count);
  }
  return result;
}
} // namespace BMMQ::Debug
