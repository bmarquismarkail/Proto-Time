#include "ScriptService.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "space/Project.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
namespace {
using namespace BMMQ;
std::vector<std::uint8_t> readRom(const std::filesystem::path &path) {
  const auto size = std::filesystem::file_size(path);
  if (!size || size > 64 * 1024 * 1024)
    throw std::invalid_argument("ROM budget rejected");
  std::vector<std::uint8_t> rom(size);
  std::ifstream input(path, std::ios::binary);
  if (!input.read(reinterpret_cast<char *>(rom.data()), rom.size()))
    throw std::runtime_error("cannot read ROM");
  return rom;
}
} // namespace
int main(int argc, char **argv) {
  try {
    using namespace BMMQ::Script;
    std::string core = "gameboy", language = "file";
    std::filesystem::path romPath, sourcePath;
    Permissions permissions;
    Limits limits;
    for (int i = 1; i < argc; ++i) {
      std::string option = argv[i];
      if (option == "--allow-ram")
        permissions.ram = true;
      else if (option == "--allow-registers")
        permissions.registers = true;
      else {
        if (i + 1 == argc)
          throw std::invalid_argument("option requires value");
        if (option == "--core")
          core = argv[++i];
        else if (option == "--language")
          language = argv[++i];
        else if (option == "--rom")
          romPath = argv[++i];
        else if (option == "--source")
          sourcePath = argv[++i];
        else
          throw std::invalid_argument("unknown option");
      }
    }
    Language selected;
    if (language == "file")
      selected = Language::File;
    else if (language == "lua")
      selected = Language::Lua;
    else if (language == "python")
      selected = Language::Python;
    else if (language == "javascript")
      selected = Language::JavaScript;
    else
      throw std::invalid_argument("unsupported runtime");
    if (romPath.empty() || sourcePath.empty())
      throw std::invalid_argument(
          "usage: time-script --core gameboy|gamegear --rom ROM --source FILE "
          "--language file|lua|python|javascript [--allow-ram] "
          "[--allow-registers]");
    auto rom = readRom(romPath);
    std::unique_ptr<Machine> machine;
    if (core == "gameboy")
      machine = std::make_unique<GB::GameBoyMachine>();
    else if (core == "gamegear")
      machine = std::make_unique<GameGearMachine>();
    else
      throw std::invalid_argument("unsupported core");
    struct Ready {
      Debug::DebugService *debugger;
      Debug::Reply initial;
    };
    std::promise<Ready> startup;
    auto future = startup.get_future();
    std::atomic<bool> stop{false}, failed{false};
    std::thread worker([&, machine = std::move(machine)] {
      std::unique_ptr<Debug::DebugService> debugger;
      bool published = false;
      try {
        machine->loadRom(rom);
        debugger = std::make_unique<Debug::DebugService>(*machine);
        Debug::Command command{.id = 1,
                               .operation = Debug::Operation::Inspect,
                               .generation = machine->observationGeneration(),
                               .pauseId = 1,
                               .address = 0xc000,
                               .length = 256};
        if (!debugger->request(command))
          throw std::runtime_error("debugger initialization failed");
        debugger->pump();
        auto reply = debugger->response();
        if (!reply || reply->error != Debug::Error::None)
          throw std::runtime_error("cannot snapshot script memory");
        startup.set_value({debugger.get(), *reply});
        published = true;
        while (!stop.load(std::memory_order_acquire)) {
          debugger->pump();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      } catch (...) {
        if (!published)
          startup.set_exception(std::current_exception());
        else {
          failed.store(true, std::memory_order_release);
          while (!stop.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    });
    struct Join {
      std::atomic<bool> &stop;
      std::thread &worker;
      ~Join() {
        stop.store(true, std::memory_order_release);
        worker.join();
      }
    } join{stop, worker};
    auto ready = future.get();
    ScriptService service;
    service.observe({core, ready.initial, 0xc000});
    if (!service.loadFile(0, selected, sourcePath, permissions, limits,
                          {.machineMutationAllowed =
                               permissions.ram || permissions.registers}) ||
        !service.invoke(0))
      throw std::runtime_error(service.diagnostics().lastRuntimeError);
    std::optional<Action> action;
    while (!(action = service.poll()))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!action->result.success)
      throw std::runtime_error(action->result.error);
    auto state = ready.initial;
    if (action->result.staged.editRegisters ||
        action->result.staged.byteCount) {
      action->result.staged.id = 2;
      if (!ready.debugger->request(action->result.staged))
        throw std::runtime_error("script command queue exhausted");
      std::optional<Debug::Reply> reply;
      while (!(reply = ready.debugger->response())) {
        if (failed.load(std::memory_order_acquire))
          throw std::runtime_error("machine lane faulted");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (!service.acknowledge(action->ticket, *reply))
        throw std::runtime_error(service.diagnostics().lastRuntimeError);
      state = *reply;
    }
    Debug::Command inspect{.id = 3,
                           .operation = Debug::Operation::Inspect,
                           .generation = state.generation,
                           .pauseId = state.pauseId,
                           .address = 0xc000,
                           .length = 256};
    if (!ready.debugger->request(inspect))
      throw std::runtime_error("script inspection queue exhausted");
    std::optional<Debug::Reply> inspected;
    while (!(inspected = ready.debugger->response())) {
      if (failed.load(std::memory_order_acquire))
        throw std::runtime_error("machine lane faulted");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (inspected->error != Debug::Error::None)
      throw std::runtime_error("script inspection rejected");
    std::cout << Space::Json{{"ok", true},
                             {"core", core},
                             {"runtime", language},
                             {"report", action->result.report},
                             {"generation", inspected->generation},
                             {"registers", inspected->registers},
                             {"memoryBase", 0xc000},
                             {"memory", inspected->memory}}
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cout
        << BMMQ::Space::Json{{"ok", false}, {"error", error.what()}}.dump()
        << '\n';
    return 1;
  }
}
