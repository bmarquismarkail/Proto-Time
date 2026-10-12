#ifdef NDEBUG
#undef NDEBUG
#endif
#include "machine/plugins/input/adapters/LinuxController.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <linux/input.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <iostream>
using namespace BMMQ;
namespace {
struct Pipe {
    int fds[2]{-1,-1};
    Pipe() { assert(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0); }
    ~Pipe() { for (auto fd : fds) if (fd >= 0) ::close(fd); }
    void event(std::uint16_t type, std::uint16_t code, std::int32_t value) {
        input_event e{}; e.type = type; e.code = code; e.value = value;
        assert(::write(fds[1], &e, sizeof(e)) == sizeof(e));
    }
    void report() { event(EV_SYN, SYN_REPORT, 0); }
};
template<class F> void await(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void verify(Machine& machine, bool gg) {
    Pipe p;
    auto owned = std::make_unique<LinuxController>(p.fds[0]);
    auto& adapter = *owned;
    assert(!adapter.capabilities().deterministic && adapter.boundedBoundarySamplingV1());
    auto& service = machine.inputService();
    assert(service.attachAdapter(std::move(owned)) && service.resume());
    const auto generation = service.currentGeneration();
    await([&] { machine.serviceInput(); return adapter.diagnostics().connected; });
    // A physical report is committed once, with both digital and analog values.
    p.event(EV_KEY, BTN_SOUTH, 1); p.event(EV_KEY, BTN_START, 1);
    p.event(EV_ABS, ABS_HAT0X, -1); p.event(EV_ABS, ABS_X, 32767); p.report();
    await([&] { machine.serviceInput(); return service.committedDigitalMask() == 0x92; });
    assert(service.committedAnalogState()->channels[0] == 32767);
    // Compare delivery through the real core against its physical input register.
    if (gg) {
        assert((machine.runtimeContext().read8(0xc000)) == 0); // Pure RAM remains untouched.
        auto& core = dynamic_cast<GameGearMachine&>(machine);
        for (int i = 0; i < 4; ++i) core.step();
        assert((core.runtimeContext().read8(0xc000) & 0x80) == 0); // Start uses logical Meta2.
        assert((core.runtimeContext().read8(0xc001) & 0x14) == 0); // Left and button 1 active-low.
    } else {
        auto& core = dynamic_cast<GB::GameBoyMachine&>(machine);
        core.runtimeContext().write8(0xff00, 0x10);
        assert((core.runtimeContext().read8(0xff00) & 9) == 0); // A + Start.
    }
    // An old-generation queued release cannot overwrite a new machine generation.
    const auto before = adapter.diagnostics().reports;
    p.event(EV_KEY, BTN_SOUTH, 0); p.report();
    await([&] { return adapter.diagnostics().reports > before; });
    service.advanceGeneration(generation + 1);
    assert(!service.pollActiveAdapter(generation));
    assert(service.pollActiveAdapter(generation + 1));
    assert(service.committedDigitalMask() == 0);
    await([&] { return service.pollActiveAdapter(generation + 1) && service.committedDigitalMask() == 0x82; });
    assert(adapter.diagnostics().staleSnapshots > 0);
    // Disconnect makes every logical channel neutral without a device syscall on polling.
    ::close(p.fds[1]); p.fds[1] = -1;
    await([&] { return !adapter.diagnostics().connected; });
    assert(service.pollActiveAdapter(generation + 1));
    assert(service.committedDigitalMask() == 0 && !service.committedAnalogState()->anyActive());
    assert(service.diagnostics().neutralFallbackCount > 0);
    assert(service.pause() && service.detachAdapter(generation + 1));
}
void failureCases() {
    // A generation heartbeat republishes only the last complete SYN_REPORT.
    { Pipe p; LinuxController adapter(p.fds[0]); assert(adapter.open());
      await([&] { return adapter.diagnostics().connected; });
      (void)adapter.sampleBoundaryV1(0);
      const auto before = adapter.diagnostics().events;
      p.event(EV_KEY, BTN_SOUTH, 1);
      await([&] { return adapter.diagnostics().events > before; });
      const auto reports = adapter.diagnostics().reports;
      (void)adapter.sampleBoundaryV1(2);
      await([&] { return adapter.diagnostics().reports > reports; });
      assert(adapter.sampleBoundaryV1(2).digital == 0);
      p.report();
      await([&] { return adapter.sampleBoundaryV1(2).digital == 0x10; });
    }
    { Pipe p; LinuxController adapter(p.fds[0]); assert(adapter.open());
      await([&] { return adapter.diagnostics().connected; });
      for (int i = 0; i < 40; ++i) p.report();
      await([&] { return adapter.diagnostics().error == LinuxControllerError::Overflow; });
      assert(adapter.sampleBoundaryV1(0).neutralFallback && adapter.diagnostics().overflows == 1);
      adapter.close(); assert(adapter.open());
      await([&] { return adapter.diagnostics().connected; });
      assert(!adapter.sampleBoundaryV1(0).neutralFallback);
    }
    { Pipe p; LinuxController adapter(p.fds[0]); assert(adapter.open());
      await([&] { return adapter.diagnostics().connected; });
      p.event(EV_SYN, SYN_DROPPED, 0); p.event(EV_KEY, BTN_SOUTH, 1); p.report();
      await([&] { return adapter.diagnostics().error == LinuxControllerError::ResyncUnavailable; });
      assert(adapter.sampleBoundaryV1(0).neutralFallback && adapter.diagnostics().droppedSynchronizations == 1);
    }
    { Pipe p; LinuxController adapter(p.fds[0]); assert(adapter.open());
      await([&] { return adapter.diagnostics().connected; });
      const char byte = 1; assert(::write(p.fds[1], &byte, 1) == 1); ::close(p.fds[1]); p.fds[1] = -1;
      await([&] { return adapter.diagnostics().error == LinuxControllerError::MalformedStream; });
      assert(adapter.sampleBoundaryV1(0).neutralFallback);
    }
    { LinuxController adapter("/definitely/missing/time-controller"); assert(adapter.open());
      await([&] { return adapter.diagnostics().error == LinuxControllerError::DeviceUnavailable; });
      assert(adapter.sampleBoundaryV1(0).neutralFallback); adapter.close();
    }
    { LinuxControllerProfile invalid; invalid.axes[0].maximum = invalid.axes[0].minimum;
      Pipe p; LinuxController adapter(p.fds[0], invalid); assert(!adapter.open()); }
}
}
int main() {
    std::vector<std::uint8_t> rom(32768);
    GB::GameBoyMachine gb; gb.loadRom(rom); verify(gb, false);
    const std::uint8_t code[]{0xdb,0x00,0x32,0x00,0xc0,0xdb,0xdc,0x32,0x01,0xc0,0x18,0xfe};
    std::copy(std::begin(code), std::end(code), rom.begin());
    GameGearMachine gg; gg.loadRom(rom); verify(gg, true);
    failureCases();
    std::cout << "Linux controller boundary handoff, both core mappings, generation and loss paths passed (event-stream surrogate)\n";
}
