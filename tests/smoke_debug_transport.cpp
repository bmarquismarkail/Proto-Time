#ifdef NDEBUG
#undef NDEBUG
#endif
#include "machine/plugins/debug/adapters/DapAdapter.hpp"
#include <cassert>
using namespace BMMQ::Debug;
using Json = BMMQ::Space::Json;
int main() {
  DapFramer framing;
  Json request = {{"seq", 1}, {"type", "request"}, {"command", "initialize"}};
  auto wire = DapAdapter::frame(request);
  for (unsigned i = 0; i < wire.size() - 1; ++i)
    assert(framing.feed(std::string_view(wire).substr(i, 1)).empty());
  auto decoded = framing.feed(std::string_view(wire).substr(wire.size() - 1));
  assert(decoded.size() == 1 && decoded[0] == request);
  decoded = framing.feed(wire + wire);
  assert(decoded.size() == 2);
  bool failed = false;
  try {
    framing.feed("Content-Length: -1\r\n\r\n{}");
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
  for (const auto core : {"gameboy", "gamegear"}) {
    DapAdapter dap(core,
                   {.state = State::Paused, .generation = 1, .pauseId = 1});
    std::vector<Json> messages;
    assert(!dap.prepare(request, messages));
    assert(messages.back()["body"]["supportsInstructionBreakpoints"] == true);
    auto send = [&](int seq, const char *command, Json args = Json::object()) {
      messages.clear();
      return dap.prepare({{"seq", seq},
                          {"type", "request"},
                          {"command", command},
                          {"arguments", args}},
                         messages);
    };
    assert(!send(2, "attach"));
    assert(messages.size() == 1 && messages[0]["event"] == "initialized");
    assert(!send(3, "configurationDone"));
    assert(messages.size() == 3);
    assert(!send(4, "scopes", {{"frameId", 1}}));
    auto reference = messages[0]["body"]["scopes"][0]["variablesReference"];
    auto edit = send(5, "setVariable",
                     {{"variablesReference", reference},
                      {"name", "AF"},
                      {"value", "0x1230"}});
    assert(edit && edit->registers[0] == 0x1230);
    auto replies = dap.complete({.id = 5,
                                 .state = State::Paused,
                                 .generation = 2,
                                 .pauseId = 2,
                                 .registers = {0x1230}});
    assert(replies[0]["success"] == true);
    assert(!send(6, "variables", {{"variablesReference", reference}}));
    assert(messages[0]["success"] == false);
    assert(!send(7, "writeMemory",
                 {{"memoryReference", "0xc000"}, {"data", "AA=A"}}));
    assert(messages[0]["success"] == false);
    assert(!send(8, "readMemory",
                 {{"memoryReference", "0xffff"},
                  {"offset", 9223372036854775807LL},
                  {"count", 1}}));
    assert(messages[0]["success"] == false);
    assert(!send(9, "readMemory",
                 {{"memoryReference", "0xc000"}, {"count", 257}}));
    assert(messages[0]["success"] == false);
    auto write = send(10, "writeMemory",
                      {{"memoryReference", "0xc000"}, {"data", "AQI="}});
    assert(write && write->byteCount == 2 && write->bytes[1].value == 2);
    auto busy = send(11, "stepIn");
    assert(!busy && messages[0]["success"] == false);
    replies = dap.complete({.id = 10,
                            .error = Error::Unsupported,
                            .state = State::Paused,
                            .generation = 2,
                            .pauseId = 2});
    assert(replies[0]["success"] == false);
    auto step = send(12, "stepIn");
    assert(step && step->steps == 1);
    replies = dap.complete(
        {.id = 12, .state = State::Running, .generation = 2, .pauseId = 2});
    assert(replies.size() == 2 && replies[1]["event"] == "continued");
    replies = dap.complete({.state = State::Paused,
                            .reason = Reason::Step,
                            .generation = 2,
                            .pauseId = 3});
    assert(replies.size() == 1 && replies[0]["event"] == "stopped");
    assert(!send(13, "stepOut"));
    assert(messages[0]["success"] == false);
    assert(!send(14, "evaluate", {{"expression", "write(0xc000, 1)"}}));
    assert(messages[0]["success"] == false);
  }
}
