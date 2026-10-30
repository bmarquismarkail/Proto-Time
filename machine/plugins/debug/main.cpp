#include "adapters/DapAdapter.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "space/Meaning.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <future>
#include <iostream>
#include <poll.h>
#include <unistd.h>
namespace {
using namespace BMMQ::Debug;
using Json = BMMQ::Space::Json;
std::vector<std::uint8_t> read(const std::filesystem::path &path,
                               std::size_t maximum) {
  auto size = std::filesystem::file_size(path);
  if (!size || size > maximum)
    throw std::invalid_argument("file budget exceeded");
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::uint8_t> bytes(size);
  if (!stream.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
    throw std::runtime_error("cannot read file");
  return bytes;
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::string core = "gameboy";
    std::filesystem::path romPath, tracePath, symbolPath;
    for (int i = 1; i < argc; ++i) {
      std::string flag = argv[i];
      if (i + 1 == argc)
        throw std::invalid_argument("option requires value");
      if (flag == "--core")
        core = argv[++i];
      else if (flag == "--rom")
        romPath = argv[++i];
      else if (flag == "--trace")
        tracePath = argv[++i];
      else if (flag == "--symbols")
        symbolPath = argv[++i];
      else
        throw std::invalid_argument("unknown option");
    }
    if (romPath.empty())
      throw std::invalid_argument(
          "usage: time-debugger --core gameboy|gamegear --rom ROM [--trace "
          "JSONL] [--symbols JSON]");
    auto rom = read(romPath, 64 * 1024 * 1024);
    auto hash = BMMQ::Space::digest(rom);
    auto symbols =
        BMMQ::Space::Project(hash, BMMQ::Space::Project::defaultBudget, core)
            .document();
    if (!symbolPath.empty()) {
      auto bytes = read(symbolPath, 4 * 1024 * 1024);
      BMMQ::Space::importSymbols(symbols,
                                 Json::parse(bytes.begin(), bytes.end()), rom);
    }
    std::unique_ptr<BMMQ::Machine> machine;
    if (core == "gameboy")
      machine = std::make_unique<GB::GameBoyMachine>();
    else if (core == "gamegear")
      machine = std::make_unique<BMMQ::GameGearMachine>();
    else
      throw std::invalid_argument("unsupported machine family");
    struct Ready {
      DebugService *service;
      Reply initial;
    };
    std::promise<Ready> startup;
    auto ready = startup.get_future();
    std::atomic<bool> shutdown{false}, failed{false};
    std::thread worker([&, machine = std::move(machine)] {
      bool published = false;
      std::unique_ptr<DebugService> debuggerOwner;
      try {
        machine->loadRom(rom);
        debuggerOwner = std::make_unique<DebugService>(*machine);
        auto &debugger = *debuggerOwner;
        Command inspect{.id = 1,
                        .operation = Operation::Inspect,
                        .generation = machine->observationGeneration(),
                        .pauseId = 1};
        debugger.request(inspect);
        debugger.pump();
        auto initial = debugger.response();
        if (!initial || initial->error != Error::None)
          throw std::runtime_error("debugger initialization failed");
        startup.set_value({&debugger, *initial});
        published = true;
        while (!shutdown.load(std::memory_order_acquire)) {
          debugger.pump();
          if (debugger.state() == State::Detached)
            break;
          if (debugger.state() == State::Running)
            (void)debugger.run(64);
          else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Wait for transport to consume final disconnect before destroying
        // queues.
        while (!shutdown.load(std::memory_order_acquire))
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } catch (...) {
        if (!published)
          startup.set_exception(std::current_exception());
        else {
          failed.store(true, std::memory_order_release);
          while (!shutdown.load(std::memory_order_acquire))
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
    } join{shutdown, worker};
    auto state = ready.get();
    DapAdapter adapter(core, state.initial, hash);
    adapter.symbols(symbols);
    DapFramer framing;
    std::ofstream trace;
    std::size_t traceBytes = 0;
    std::uint64_t lost = 0, profileLost = 0;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> profile;
    if (!tracePath.empty()) {
      trace.open(tracePath);
      if (!trace)
        throw std::runtime_error("cannot open trace");
      auto header = Json{{"schemaVersion", 1},
                         {"core", core},
                         {"romSha256", hash},
                         {"backend", "baseline"},
                         {"kind", "header"}}
                        .dump() +
                    "\n";
      trace << header;
      traceBytes += header.size();
    }
    const auto writeMessage = [&](const Json &message) {
      std::cout << DapAdapter::frame(message) << std::flush;
      if (!std::cout)
        throw std::runtime_error("DAP output disconnected");
    };
    const auto writeTrace = [&](const Json &record) {
      if (!trace.is_open())
        return;
      auto line = record.dump() + "\n";
      if (traceBytes + line.size() > 100 * 1024 * 1024 - 512) {
        trace
            << Json{{"kind", "gap"}, {"reason", "file budget exhausted"}}.dump()
            << '\n';
        trace.close();
        std::cerr << "trace storage exhausted; inspection continues with lost "
                     "file evidence\n";
        return;
      }
      trace << line;
      if (!trace) {
        trace.close();
        std::cerr << "trace sink failed; inspection continues with lost file "
                     "evidence\n";
        return;
      }
      traceBytes += line.size();
    };
    std::signal(SIGPIPE, SIG_IGN);
    bool finished = false;
    while (!finished && !failed.load(std::memory_order_acquire)) {
      while (auto reply = state.service->response()) {
        for (const auto &message : adapter.complete(*reply))
          writeMessage(message);
        if (reply->lost != lost) {
          writeTrace({{"kind", "gap"},
                      {"lostTotal", reply->lost},
                      {"generation", reply->generation}});
          lost = reply->lost;
        }
        if (reply->state == State::Detached)
          finished = true;
      }
      while (auto record = state.service->trace()) {
        Json value = {{"kind", unsigned(record->kind)},
                      {"generation", record->generation},
                      {"instruction", record->instruction},
                      {"backing", record->backing}};
        if (record->kind == TraceKind::Access) {
          value["access"] = unsigned(record->access);
          value["address"] = record->address;
          value["value"] = record->value;
        }
        if (record->kind == TraceKind::Retirement) {
          value["pcBefore"] = record->feedback.pcBefore;
          value["pcAfter"] = record->feedback.pcAfter;
          value["cycles"] = record->feedback.retiredCycles;
          value["registers"] = record->registers;
          if (profile.size() < 65536 || profile.contains(record->backing)) {
            auto &tally = profile[record->backing];
            ++tally.first;
            tally.second += record->feedback.retiredCycles;
          } else
            ++profileLost;
        }
        writeTrace(value);
      }
      if (finished)
        break;
      pollfd descriptor{STDIN_FILENO, POLLIN, 0};
      auto available = poll(&descriptor, 1, 5);
      if (available < 0) {
        if (errno == EINTR)
          continue;
        throw std::runtime_error("DAP input failed");
      }
      if (available && descriptor.revents & (POLLIN | POLLHUP)) {
        std::array<char, 4096> bytes{};
        auto count = ::read(STDIN_FILENO, bytes.data(), bytes.size());
        if (count <= 0) {
          if (count < 0 && errno == EINTR)
            continue;
          if (framing.incomplete())
            throw std::invalid_argument("truncated DAP frame");
          break;
        }
        for (const auto &request :
             framing.feed(std::string_view(bytes.data(), count))) {
          std::vector<Json> messages;
          auto command = adapter.prepare(request, messages);
          for (auto &message : messages)
            writeMessage(message);
          if (command && !state.service->request(*command)) {
            // Finish the pending request explicitly; never leave the client
            // waiting.
            auto rejected = adapter.current();
            rejected.id = command->id;
            rejected.error = Error::Invalid;
            for (auto &message : adapter.complete(rejected))
              writeMessage(message);
          }
        }
      }
    }
    if (profileLost)
      writeTrace({{"kind", "gap"},
                  {"reason", "profile location budget exhausted"},
                  {"lost", profileLost}});
    for (const auto &[backing, tally] : profile)
      writeTrace({{"kind", "profile"},
                  {"backing", backing},
                  {"instructions", tally.first},
                  {"cycles", tally.second},
                  {"complete", lost == 0 && !profileLost && finished}});
    writeTrace({{"kind", "footer"},
                {"complete", lost == 0 && !profileLost && finished},
                {"lostRecords", lost},
                {"lostProfileEntries", profileLost}});
    return failed.load(std::memory_order_acquire) ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
