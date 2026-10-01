#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/modding/NativeMod.hpp"
#include <cassert>
#include <filesystem>
#include <thread>
#include <unistd.h>
int main(int argc, char **argv) {
  assert(argc == 3);
  using namespace BMMQ::Modding;
  auto host = std::make_shared<ModHost>();
  auto id = host->createRegion("pool", 4);
  LoadedMod metadata{"fixture", "1", 0, {{"pool", id}}, argv[1]};
  auto mod = NativeMod::load(metadata, host);
  TimeModObservationV1 o{
      sizeof(o),
      1,
      7,
      "rom",
      "application",
      nullptr,
      [](void *, uint32_t address, uint8_t *bytes, uint32_t size) -> int32_t {
        assert(address == 0xc000 && size == 1);
        *bytes = 10;
        return 1;
      }};
  assert(mod->observe(o));
  TimeModCallV1 call{sizeof(call), 3, 0, 0, 0};
  assert(mod->invoke(call) && call.result == 17);
  o.generation = 9;
  assert(mod->observe(o));
  assert(mod->invoke(call) && call.result == 19);
  bool rejected = false;
  std::thread wrong([&] {
    try {
      mod->observe(o);
    } catch (...) {
      rejected = true;
    }
  });
  wrong.join();
  assert(rejected);
  o.struct_size = 0;
  assert(!mod->observe(o));
  metadata.nativeModule = argv[2];
  auto legacy = NativeMod::load(metadata, host);
  assert(legacy->observe(o));
  GB::GameBoyMemoryMap memory;
  memory.write(0xfffe, 0x55);
  memory.write(0xffff, 0x1f);
  assert(memory.read(0xfffe) == 0x55 && memory.read(0xffff) == 0x1f);
  memory.setIoRegisterRaw(0xffff, 0x12);
  assert(memory.read(0xffff) == 0x12);
  GB::GameBoyMachine machine;
  std::vector<uint8_t> rom(32768);
  auto generation = machine.observationGeneration();
  machine.loadRom(rom);
  assert(machine.observationGeneration() > generation);
  auto path = std::filesystem::temp_directory_path() /
              ("observer-state-" + std::to_string(getpid()));
  machine.save_state(path);
  generation = machine.observationGeneration();
  machine.load_state(path);
  assert(machine.observationGeneration() > generation);
  std::filesystem::remove(path);
  generation = machine.observationGeneration();
  machine.loadRom(rom);
  assert(machine.observationGeneration() > generation);
}
