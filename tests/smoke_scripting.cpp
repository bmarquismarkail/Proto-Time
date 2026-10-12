#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/plugins/script/ScriptService.hpp"
#include "space/Project.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace BMMQ;
using namespace BMMQ::Script;
namespace {
Debug::Reply call(Debug::DebugService &debugger, Debug::Command command,
                  const Debug::Reply &state) {
  command.generation = state.generation;
  command.pauseId = state.pauseId;
  assert(debugger.request(command));
  debugger.pump();
  auto reply = debugger.response();
  assert(reply);
  return *reply;
}
std::optional<Action> wait(ScriptService &service) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < end) {
    if (auto result = service.poll())
      return result;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(false);
  return {};
}
std::string recipe(std::string_view core) {
  return Space::Json{
      {"schemaVersion", 1},
      {"core", core},
      {"actions",
       {{{"operation", "register"}, {"name", "BC"}, {"value", 1234}},
        {{"operation", "write"}, {"address", 49152}, {"value", 85}},
        {{"operation", "report"}, {"text", "done"}}}}}
      .dump();
}
void verify(Machine &machine, std::string core) {
  Debug::DebugService debugger(machine);
  auto &adapter = dynamic_cast<Debug::IDebugMachineV1 &>(machine);
  auto state =
      call(debugger,
           {.operation = Debug::Operation::Inspect,
            .address = 0xc000,
            .length = 256},
           {.generation = machine.observationGeneration(), .pauseId = 1});
  const auto original = adapter.debugRegisters();
  std::uint8_t originalByte;
  assert(adapter.debugPeek(0xc000, originalByte));
  Snapshot view{core, state, 0xc000};
  Limits normal;
  if (const char *configured = std::getenv("TIME_SCRIPT_TEST_TIMEOUT_MS")) {
    const auto milliseconds = std::stoll(configured);
    assert(milliseconds >= 1 && milliseconds <= 30000);
    normal.timeout = std::chrono::milliseconds(milliseconds);
  }
  for (auto language : {Language::File, Language::Lua, Language::Python,
                        Language::JavaScript}) {
    std::string source;
    switch (language) {
    case Language::File:
      source = recipe(core);
      break;
    case Language::Lua:
      source = "time.setreg('BC',1234); time.write8(49152,85); "
               "assert(time.read8(49152)==85); time.report('done')";
      break;
    case Language::Python:
      source = "time.setreg('BC',1234)\ntime.write8(49152,85)\nassert "
               "time.read8(49152)==85\ntime.report('done')";
      break;
    case Language::JavaScript:
      source =
          "time.setreg('BC',1234); time.write8(49152,85); "
          "if(time.read8(49152)!==85)throw Error('read'); time.report('done');";
      break;
    }
    if (!ScriptEngine::available(language)) {
      assert(
          !ScriptEngine::evaluate(language, source, view, {}, normal).success);
      std::cout << "runtime unavailable: " << unsigned(language) << '\n';
      continue;
    }
    auto result = ScriptEngine::evaluate(language, source, view,
                                         {true, true, true}, normal);
    if (!result.success)
      std::cerr << "runtime " << unsigned(language) << ": " << result.error
                << '\n';
    assert(result.success && result.report == "done" &&
           result.staged.byteCount == 1 && result.staged.registers[1] == 1234);
    assert(adapter.debugRegisters() == original);
    std::uint8_t value;
    assert(adapter.debugPeek(0xc000, value) && value == originalByte);
    auto denied = ScriptEngine::evaluate(language, source, view, {}, normal);
    assert(!denied.success && denied.permissionDenied);
    std::string fail, loop, heap;
    if (language == Language::File) {
      fail = recipe(core == "gameboy" ? "gamegear" : "gameboy");
      loop = recipe(core);
      heap = recipe(core);
    } else if (language == Language::Lua) {
      fail = "time.write8(49152,42); error('failure')";
      loop = "while true do end";
      heap = "local s=string.rep('x',1000000)";
    } else if (language == Language::Python) {
      fail = "time.write8(49152,42)\nraise ValueError('failure')";
      loop = "while True: pass";
      heap = "s='x'*1000000";
    } else {
      fail = "time.write8(49152,42); throw Error('failure')";
      loop = "for(;;){}";
      heap = "let a=[]; for(let i=0;i<1000000;i++)a.push(i);";
    }
    result = ScriptEngine::evaluate(language, fail, view, {true, true, true},
                                    normal);
    assert(!result.success && !result.staged.byteCount &&
           !result.staged.editRegisters);
    if (language != Language::File)
      assert(result.error.find("failure") != std::string::npos);
    Limits bounded;
    bounded.instructions = 1;
    bounded.timeout = std::chrono::milliseconds(500);
    assert(!ScriptEngine::evaluate(language, loop, view, {true, true, true},
                                   bounded)
                .success);
    bounded = normal;
    bounded.heapBytes = 65536;
    if (language != Language::File)
      assert(!ScriptEngine::evaluate(language, heap, view, {true, true, true},
                                     bounded)
                  .success);
  }
  if (ScriptEngine::available(Language::Lua))
    assert(!ScriptEngine::evaluate(Language::Lua,
                                   "pcall(function()time.write8(-1,2)end)",
                                   view, {true, true, true}, normal)
                .success);
  if (ScriptEngine::available(Language::Lua))
    assert(!ScriptEngine::evaluate(Language::Lua,
                                   "local x=setmetatable({}, "
                                   "{__gc=function()time.write8(49152,1)end})",
                                   view, {true, true, true}, normal)
                .success);
  if (ScriptEngine::available(Language::JavaScript)) {
    assert(!ScriptEngine::evaluate(Language::JavaScript,
                                   "try{time.write8(-1,2)}catch(e){}", view,
                                   {true, true, true}, normal)
                .success);
    assert(!ScriptEngine::evaluate(Language::JavaScript,
                                   "Promise.reject(Error('failure'))", view, {},
                                   normal)
                .success);
    assert(
        ScriptEngine::evaluate(Language::JavaScript,
                               "Promise.resolve().then(()=>time.report('job'))",
                               view, {}, normal)
            .report == "job");
  }
  if (ScriptEngine::available(Language::Python)) {
    assert(!ScriptEngine::evaluate(
                Language::Python,
                "try: time.write8(-1,2)\nexcept ValueError: pass", view,
                {true, true, true}, normal)
                .success);
    Limits bounded;
    bounded.timeout = std::chrono::milliseconds(150);
    assert(!ScriptEngine::evaluate(Language::Python,
                                   "import time as host\nhost.sleep(10)", view,
                                   {}, bounded)
                .success);
  }
  assert(!ScriptEngine::evaluate(Language::File,
                                 std::string(100, '[') + std::string(100, ']'),
                                 view)
              .success);
  assert(!ScriptEngine::evaluate(Language::File, recipe(core),
                                 {core, {.state = Debug::State::Running}, 0})
              .success);
  ScriptService service;
  assert(!service.invoke(0));
  service.observe(view);
  assert(!service.load(0, Language::File, recipe(core), {true, true, true}));
  assert(service.load(0, Language::File, recipe(core), {true, true, true}, {},
                      {.deterministic = true, .machineMutationAllowed = true}));
  assert(service.invoke(0));
  assert(!service.invoke(0));
  assert(!service.unload(0));
  auto action = wait(service);
  assert(action && action->result.success);
  assert(!service.invoke(0));
  // Add an unsupported device write: the complete recipe must be rejected.
  auto invalid = action->result.staged;
  invalid.bytes[invalid.byteCount++] = {0x8000, 1};
  auto rejected = call(debugger, invalid, state);
  assert(rejected.error == Debug::Error::Unsupported);
  assert(!service.acknowledge(action->ticket, rejected));
  assert(adapter.debugRegisters() == original);
  assert(service.invoke(0));
  action = wait(service);
  state = call(debugger, action->result.staged, state);
  assert(state.error == Debug::Error::None &&
         service.acknowledge(action->ticket, state));
  assert(!service.acknowledge(action->ticket, state));
  assert(adapter.debugRegisters()[1] == 1234);
  std::uint8_t value;
  assert(adapter.debugPeek(0xe000, value) && value == 85);
  view.state = state;
  view.state.length = 0;
  service.observe(view);
  assert(service.invoke(0));
  ++view.state.generation;
  service.observe(view);
  action = wait(service);
  assert(!action->result.success && !action->result.staged.byteCount);
  view.state = state;
  service.observe(view);
  assert(!service.invokeLive(0));
  assert(service.diagnostics().rejectedLiveCallbackCount == 1);
  assert(service.invoke(0));
  service.detach();
  action = wait(service);
  assert(!action->result.success);
  assert(service.unload(0));
  bool wrongLane = false;
  std::thread wrong([&] {
    try {
      service.detach();
    } catch (const std::logic_error &) {
      wrongLane = true;
    }
  });
  wrong.join();
  assert(wrongLane);
  const HookCapabilities caps{true, true, true, true};
  std::array<HookAction, 2> hooks{{{1, 1, false}, {3, 100, true}}};
  assert(!debugger.installScriptHooks(hooks, {}));
  assert(debugger.installScriptHooks(hooks, caps));
  state = call(debugger, {.operation = Debug::Operation::Continue}, state);
  assert(!debugger.installScriptHooks(hooks, caps));
  auto progress = debugger.run(20);
  assert(progress.progress.retiredInstructions == 3);
  auto paused = debugger.response();
  assert(paused && paused->state == Debug::State::Paused);
  assert(debugger.scriptHooks().counters()[0] == 3 &&
         debugger.scriptHooks().counters()[1] == 1);
  state = *paused;
  auto edit =
      Debug::Command{.operation = Debug::Operation::Edit, .byteCount = 1};
  edit.bytes[0] = {0xc000, 1};
  state = call(debugger, edit, state);
  assert(debugger.scriptHooks().invalidations() == 1);
  state = call(debugger, {.operation = Debug::Operation::Continue}, state);
  progress = debugger.run(4);
  assert(progress.progress.retiredInstructions == 4);
  assert(debugger.state() == Debug::State::Running);
  state = call(debugger, {.operation = Debug::Operation::Pause}, state);
  assert(state.state == Debug::State::Paused);
}
} // namespace
int main() {
  std::vector<std::uint8_t> rom(32768);
  GB::GameBoyMachine gb;
  gb.loadRom(rom);
  verify(gb, "gameboy");
  BMMQ::GameGearMachine gg;
  gg.loadRom(rom);
  verify(gg, "gamegear");
  std::cout << "owned scripting, atomic recipes, runtime budgets and live "
               "prepared hooks passed\n";
}
