#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/plugins/debug/DebugPlugin.hpp"
#include "machine/plugins/debug/DebugService.hpp"
#include "space/Capture.hpp"
#include <cassert>
#include <filesystem>
#include <thread>
using namespace BMMQ::Debug;
namespace {
Reply call(DebugService &service, Command command, const Reply &current) {
  command.generation = current.generation;
  command.pauseId = current.pauseId;
  assert(service.request(command));
  service.pump();
  auto reply = service.response();
  assert(reply && reply->id == command.id);
  return *reply;
}
void verify(BMMQ::Machine &machine, bool gg) {
  auto &adapter = dynamic_cast<IDebugMachineV1 &>(machine);
  const auto before = adapter.debugRegisters();
  DebugService service(machine);
  bool rejected = false;
  try {
    machine.step();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  assert(adapter.debugRegisters() == before);
  Reply current{.generation = machine.observationGeneration(), .pauseId = 1};
  current = call(service, {.id = 1, .operation = Operation::Inspect}, current);
  assert(current.error == Error::None);
  const auto entry = current.registers[5];
  Command rules{.id = 2, .operation = Operation::Rules};
  rules.breakCount = 1;
  rules.breaks[0] = {std::uint16_t(entry + 2),
                     adapter.debugBacking(std::uint16_t(entry + 2)), true};
  current = call(service, rules, current);
  assert(current.error == Error::None);
  current = call(service, {.id = 3, .operation = Operation::Continue}, current);
  assert(current.state == State::Running);
  auto progress = service.run(20);
  assert(progress.progress.retiredInstructions == 1);
  auto stopped = service.response();
  assert(stopped && stopped->reason == Reason::Breakpoint);
  current = *stopped;
  assert(current.registers[5] == entry + 2);
  rules.breakCount = 0;
  rules.watchCount = 1;
  rules.watches[0] = {0xc000, 0xc000, Access::Write};
  rules.id = 4;
  current = call(service, rules, current);
  current = call(service, {.id = 5, .operation = Operation::Continue}, current);
  progress = service.run(20);
  assert(progress.progress.retiredInstructions == 1);
  stopped = service.response();
  assert(stopped && stopped->reason == Reason::Watchpoint);
  current = *stopped;
  std::uint8_t value;
  assert(adapter.debugPeek(0xc000, value) && value == 0x42);
  // Inspection is side-effect free and does not publish access records.
  while (service.trace()) {
  }
  current = call(service,
                 {.id = 6,
                  .operation = Operation::Inspect,
                  .address = 0xc000,
                  .length = 1},
                 current);
  assert(current.length == 1 && current.memory[0] == 0x42);
  assert(!service.trace());
  auto unchanged = adapter.debugRegisters();
  Command edit{.id = 7, .operation = Operation::Edit};
  edit.editRegisters = true;
  edit.registers = unchanged;
  edit.registers[0] = 0x4300;
  edit.byteCount = 2;
  edit.bytes[0] = {0xc001, 0x99};
  edit.bytes[1] = {0x8000, 1};
  auto reject = call(service, edit, current);
  assert(reject.error == Error::Unsupported);
  assert(adapter.debugRegisters() == unchanged);
  assert(adapter.debugPeek(0xc001, value) && value != 0x99);
  edit.byteCount = 1;
  current = call(service, edit, current);
  assert(current.error == Error::None);
  assert(current.generation == machine.observationGeneration());
  assert(current.registers[0] == 0x4300);
  assert(adapter.debugPeek(0xe001, value) && value == 0x99);
  edit.id = 8;
  edit.generation = current.generation - 1;
  edit.pauseId = current.pauseId;
  assert(service.request(edit));
  service.pump();
  reject = *service.response();
  assert(reject.error == Error::Stale);
  edit.generation = current.generation;
  edit.pauseId = current.pauseId - 1;
  assert(service.request(edit));
  service.pump();
  reject = *service.response();
  assert(reject.error == Error::Stale);
  // Register aliases/shadow flags are validated before any RAM is changed.
  edit.registers = adapter.debugRegisters();
  if (gg)
    edit.registers[17] = 3;
  else
    edit.registers[0] |= 1;
  reject = call(service, edit, current);
  assert(reject.error == Error::Invalid);
  const auto path = std::filesystem::temp_directory_path() /
                    (gg ? "time-debug-gg.state" : "time-debug-gb.state");
  machine.save_state(path);
  current = call(service, {.id = 9, .operation = Operation::Step}, current);
  service.run(1);
  stopped = service.response();
  assert(stopped && stopped->reason == Reason::Step);
  current = *stopped;
  machine.load_state(path);
  service.pump();
  Command stale{.id = 10,
                .operation = Operation::Continue,
                .generation = current.generation,
                .pauseId = current.pauseId};
  assert(service.request(stale));
  service.pump();
  reject = *service.response();
  assert(reject.error == Error::Stale);
  current = {.generation = machine.observationGeneration(),
             .pauseId = reject.pauseId};
  current = call(service, {.id = 11, .operation = Operation::Inspect}, current);
  assert(current.reason == Reason::Generation);
  std::filesystem::remove(path);
  BMMQ::Space::Capture capture;
  rejected = false;
  try {
    if (gg)
      dynamic_cast<BMMQ::GameGearMachine &>(machine).setAnalysisCapture(
          &capture);
    else
      dynamic_cast<GB::GameBoyMachine &>(machine).setAnalysisCapture(&capture);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  std::thread wrong([&] {
    bool failed = false;
    try {
      service.run(1);
    } catch (const std::logic_error &) {
      failed = true;
    }
    assert(failed);
  });
  wrong.join();
  current =
      call(service, {.id = 12, .operation = Operation::Disconnect}, current);
  assert(current.state == State::Detached);
  machine.step(); // Ordinary execution resumes after detaching the control
                  // service.
}
} // namespace
class Sink final : public ITraceSinkPluginV1, public IDebuggerPluginV1 {
public:
  bool compatible = true, broken = false;
  int *calls;
  int *closes;
  Sink(int &count, int &close) : calls(&count), closes(&close) {}
  std::string_view name() const noexcept override {
    return "test-debug-backend";
  }
  DebugCapabilitiesV1 capabilities() const noexcept override {
    return {.snapshotAware = compatible, .canRequestPause = true};
  }
  bool open() override { return true; }
  void close() noexcept override { ++*closes; }
  bool consume(const TraceRecord &) override {
    ++*calls;
    if (broken)
      throw std::runtime_error("disconnected sink");
    return true;
  }
  std::optional<Command> inspect(const Reply &reply) override {
    return Command{.id = 1,
                   .operation = Operation::Pause,
                   .generation = reply.generation,
                   .pauseId = reply.pauseId};
  }
};
int main() {
  {
    DebugBackendService backends;
    int calls = 0, closes = 0;
    auto incompatible = std::make_unique<Sink>(calls, closes);
    incompatible->compatible = false;
    assert(!backends.attach(std::move(incompatible)));
    assert(backends.diagnostics().compatibilityBypasses == 1);
    auto bad = std::make_unique<Sink>(calls, closes);
    bad->broken = true;
    assert(backends.attach(std::move(bad)));
    assert(backends.attach(std::make_unique<Sink>(calls, closes)));
    backends.publish(TraceRecord{});
    assert(calls == 2 && closes == 1);
    assert(backends.diagnostics().sinkFailures == 1);
    backends.publish(TraceRecord{});
    assert(calls == 3 && closes == 1);
    Reply snapshot{.state = State::Paused, .generation = 5, .pauseId = 3};
    backends.publish(snapshot);
    auto action = backends.action();
    assert(action && action->operation == Operation::Pause &&
           action->generation == 5);
    for (unsigned i = 0; i < 33; ++i)
      backends.publish(snapshot);
    assert(backends.diagnostics().lostActions == 1);
    backends.clear();
    assert(closes == 2 && !backends.action());
  }

  std::vector<std::uint8_t> gb(32768, 0), gg(32768, 0);
  const std::uint8_t gp[] = {0x3e, 0x42, 0xea, 0x00, 0xc0, 0x00, 0x18, 0xfe};
  std::copy(std::begin(gp), std::end(gp), gb.begin() + 0x100);
  const std::uint8_t zp[] = {0x3e, 0x42, 0x32, 0x00, 0xc0, 0x00, 0x18, 0xfe};
  std::copy(std::begin(zp), std::end(zp), gg.begin());
  GB::GameBoyMachine gameboy;
  gameboy.loadRom(gb);
  verify(gameboy, false);
  BMMQ::GameGearMachine gamegear;
  gamegear.loadRom(gg);
  verify(gamegear, true);
  {
    BMMQ::GameGearMachine ports;
    auto portRom = gg;
    const uint8_t program[] = {0x3e, 0x42, 0xd3, 0x06, 0x00};
    std::copy(std::begin(program), std::end(program), portRom.begin());
    ports.loadRom(portRom);
    DebugService debugger(ports);
    Reply current{.generation = ports.observationGeneration(), .pauseId = 1};
    Command watches{.id = 1, .operation = Operation::Rules};
    watches.watchCount = 1;
    watches.watches[0] = {0x4206, 0x4206, Access::PortWrite};
    current = call(debugger, watches, current);
    assert(current.error == Error::None);
    current =
        call(debugger, {.id = 2, .operation = Operation::Continue}, current);
    auto steps = debugger.run(8);
    assert(steps.progress.retiredInstructions == 2);
    auto stopped = debugger.response();
    assert(stopped && stopped->reason == Reason::Watchpoint);
    bool observed = false;
    while (auto record = debugger.trace())
      if (record->kind == TraceKind::Access &&
          record->access == Access::PortWrite) {
        assert(record->address == 0x4206 && record->value == 0x42);
        observed = true;
      }
    assert(observed);
    current = *stopped;
    // Saturate request/reply queues without dropping accepted requests.
    for (unsigned i = 1; i <= 32; ++i) {
      Command c{.id = 100 + i,
                .generation = current.generation,
                .pauseId = current.pauseId};
      assert(debugger.request(c));
    }
    assert(!debugger.request({}));
    debugger.pump();
    for (unsigned i = 1; i <= 32; ++i) {
      auto reply = debugger.response();
      assert(reply && reply->id == 100 + i && reply->requestOverflow == 1);
    }
  }
  {
    GB::GameBoyMachine unloaded;
    DebugService debugger(unloaded);
    Reply current{.generation = unloaded.observationGeneration(), .pauseId = 1};
    current =
        call(debugger, {.id = 1, .operation = Operation::Continue}, current);
    debugger.run(1);
    auto fault = debugger.response();
    assert(fault && fault->state == State::Faulted &&
           fault->reason == Reason::Fault);
    current = *fault;
    auto rejected =
        call(debugger, {.id = 2, .operation = Operation::Step}, current);
    assert(rejected.error == Error::Fault && rejected.rejectedActions == 1);
    auto detached =
        call(debugger, {.id = 3, .operation = Operation::Disconnect}, current);
    assert(detached.state == State::Detached);
  }
  DebugEngine engine;
  engine.boundary(1);
  engine.begin();
  for (unsigned i = 0; i < 2048; ++i)
    engine.access(Access::Write, 0xc000, 1, 0);
  assert(engine.lost() == 1025);
  unsigned count = 0;
  while (engine.consume())
    ++count;
  assert(count == 1024);
  std::array<Breakpoint, 65> oversized{};
  assert(!engine.rules(oversized, {}));
  // SPSC handoff under actual concurrent publication/consumption (TSAN target).
  Queue<Command, 32> queue;
  std::thread producer([&] {
    for (unsigned i = 1; i <= 20000; ++i) {
      Command c{.id = i};
      while (!queue.push(c))
        std::this_thread::yield();
    }
  });
  for (unsigned expected = 1; expected <= 20000; ++expected) {
    std::optional<Command> c;
    while (!(c = queue.pop()))
      std::this_thread::yield();
    assert(c->id == expected);
  }
  producer.join();
}
