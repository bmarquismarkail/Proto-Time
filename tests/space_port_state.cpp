#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/plugins/input/InputPlugin.hpp"
#include "space/Project.hpp"
#include <fstream>
#include <iostream>
using namespace BMMQ::Space;
class ScriptInput final : public BMMQ::IDigitalInputSourcePlugin {
public:
  BMMQ::InputButtonMask mask = 0;
  BMMQ::InputPluginCapabilities capabilities() const noexcept override {
    return {true, false, true, true, false, true, false, false, false, true};
  }
  std::string_view name() const noexcept override {
    return "space-fixture-input";
  }
  bool open() override { return true; }
  void close() noexcept override {}
  std::string_view lastError() const noexcept override { return {}; }
  std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override {
    return mask;
  }
};
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw std::invalid_argument(
          "usage: time-space-port-state GG_ROM SCENARIOS OUTPUT");
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> rom{(std::istreambuf_iterator<char>(f)), {}};
    ScriptInput adapter;
    BMMQ::GameGearMachine machine;
    machine.loadRom(rom);
    if (!machine.inputService().attachExternalAdapter(adapter) ||
        !machine.inputService().resume())
      throw std::runtime_error("fixture input attach failed");
    auto commands = Project::read(argv[2]);
    uint64_t steps = 0, frames = 0;
    auto read = [&](uint16_t a) { return machine.runtimeContext().peek8(a); };
    auto tick = [&]() { return uint16_t(read(0xc010) | (read(0xc011) << 8)); };
    auto wait = [&](auto predicate) {
      uint64_t n = 0;
      while (!predicate()) {
        if (++steps > 100000000 || ++n > 1000000)
          throw std::runtime_error("fixture run bound");
        machine.step();
      }
    };
    wait([&]() { return read(0xc00c) == 1 && tick() >= 2; });
    Json states = Json::array();
    for (auto &c : commands) {
      auto mask = c.at("mask").get<uint8_t>();
      adapter.mask = mask;
      machine.inputService().publishDigitalSnapshot(
          mask, machine.inputService().currentGeneration());
      machine.serviceInput();
      auto count = c.at("frames").get<unsigned>();
      frames += count;
      if (frames > 4096)
        throw std::invalid_argument("fixture frame bound");
      for (unsigned i = 0; i < count; ++i) {
        auto end = uint16_t(tick() + 1);
        wait([&]() { return tick() >= end; });
        Json s = {{"commit", tick()}};
        for (auto [name, address] :
             std::initializer_list<std::pair<const char *, uint16_t>>{
                 {"x", 0xc000},
                 {"y", 0xc001},
                 {"picked", 0xc002},
                 {"score", 0xc003},
                 {"won", 0xc004},
                 {"pose", 0xc005},
                 {"phase", 0xc006},
                 {"input", 0xc007},
                 {"previous", 0xc008},
                 {"remaining", 0xc00a},
                 {"note", 0xc00b},
                 {"bank", 0xc012},
                 {"blocked", 0xc020},
                 {"collected", 0xc021},
                 {"victory", 0xc022},
                 {"lastCue", 0xc023},
                 {"sound", 0xc024}})
          s[name] = read(address);
        states.push_back(s);
      }
    }
    Project::write(argv[3], {{"status", "passed"},
                             {"romSha256", digest(rom)},
                             {"states", states},
                             {"steps", decimal(steps)}});
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
