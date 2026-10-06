#include "space/Porting.hpp"
#include "space/Session.hpp"
#include <fstream>
#include <iostream>
using namespace BMMQ::Space;
namespace {
uint8_t ram(GB::GameBoyMachine &m, uint16_t address) {
  uint8_t b = 0;
  m.executionMemory().read(&b, address, 1);
  return b;
}
uint16_t commit(GB::GameBoyMachine &m) {
  return uint16_t(ram(m, 0xc010) | (ram(m, 0xc011) << 8));
}
void input(GB::GameBoyMachine &m, uint8_t mask) {
  m.inputService().publishDigitalSnapshot(mask,
                                          m.inputService().currentGeneration());
  m.setJoypadState(mask);
}
} // namespace
// Fixture-only deterministic driver: no model exploration or guessed entry
// points.
int main(int argc, char **argv) {
  Json report = {{"status", "incomplete"},
                 {"windows", Json::array()},
                 {"intentionalOmissions", Json::array()}};
  try {
    if (argc != 5)
      throw std::invalid_argument(
          "usage: time-space-port-capture ROM SCENARIOS WINDOWS OUTPUT_DIR");
    std::filesystem::path out = argv[4];
    std::filesystem::create_directories(out);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> rom{(std::istreambuf_iterator<char>(file)), {}};
    if (rom.size() != 65536)
      throw std::invalid_argument("fixture ROM must be 64 KiB");
    auto scenarios = Project::read(argv[2]), windows = Project::read(argv[3]);
    if (!scenarios.is_array() || !windows.is_array() || windows.empty() ||
        windows.size() > 16)
      throw std::invalid_argument("fixture capture bounds");
    GB::GameBoyMachine traced, plain;
    traced.loadRom(rom);
    plain.loadRom(rom);
    auto budget = std::make_shared<StateBudget>();
    if (!windows.empty() && windows[0].contains("budget")) {
      auto n = windows[0]["budget"].get<size_t>();
      if (n > Project::defaultBudget)
        throw std::invalid_argument("capture budget maximum");
      budget->maximum = n;
    }
    Project aggregate(digest(rom));
    aggregate.setBudget(budget);
    uint64_t steps = 0, omitted = 0, totalFrames = 0;
    size_t wi = 0;
    Json ticks = Json::array();
    uint16_t recorded = 2;
    auto sampleTick = [&]() {
      if (ram(traced, 0xc00c) != 1)
        return;
      auto current = commit(traced);
      if (current <= recorded)
        return;
      recorded = current;
      Json state = {{"commit", current}};
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
        state[name] = ram(traced, address);
      ticks.push_back(state);
    };
    auto advance = [&]() {
      if (++steps > 100000000)
        throw std::runtime_error("scenario step bound");
      traced.step();
      plain.step();
      sampleTick();
    };
    auto wait = [&](auto predicate) {
      uint64_t n = 0;
      while (!predicate()) {
        if (++n > 1000000)
          throw std::runtime_error("trigger wait exceeded 1000000 steps");
        advance();
      }
      return n;
    };
    auto window = [&](const Json &spec) {
      auto count = spec.value("count", 32u);
      if (!count || count > 128)
        throw std::invalid_argument("window instruction bound");
      uint16_t pc = spec.at("pc").get<uint16_t>();
      auto bank = spec.value("bank", 0u);
      auto skipped = wait([&]() {
        return traced.executionMemory().file.findRegister("PC")->reg->value ==
                   pc &&
               (pc < 0x4000 || ram(traced, 0xc012) == bank);
      });
      if (steps > omitted)
        report["intentionalOmissions"].push_back(
            {{"fromStep", decimal(omitted)},
             {"toStep", decimal(steps)},
             {"reason", "intentional execution without capture; fresh history "
                        "on next window"}});
      auto ledgerPath =
          std::filesystem::path(argv[1]).parent_path() / "ledger.json";
      if (std::filesystem::exists(ledgerPath)) {
        auto before = traced.deterministicStateFingerprint();
        auto offline = Project(digest(rom)).document();
        attachPorting(offline, Project::read(ledgerPath));
        (void)checkPorting(offline);
        (void)queryPorting(offline, {{"type", "inventory"}, {"limit", 1}});
        if (before != traced.deterministicStateFingerprint())
          throw std::runtime_error(
              "offline accounting/query changed guest fingerprint or cycles");
        report["accountingFingerprintParity"] = "passed";
      }
      Json document;
      uint64_t start = steps;
      {
        Session session(traced, rom, {}, budget);
        for (unsigned i = 0; i < count; ++i) {
          session.step();
          plain.step();
          ++steps;
          sampleTick();
          if (traced.deterministicStateFingerprint() !=
              plain.deterministicStateFingerprint())
            throw std::runtime_error(
                "capture-on/off state/cycle parity failed");
        }
        document = session.document();
      }
      if (!document["gaps"].empty())
        throw std::runtime_error("capture evidence loss/budget exhaustion");
      aggregate.merge(document);
      omitted = steps;
      report["windows"].push_back(
          {{"id", document["sessions"][0]},
           {"name", spec.at("name")},
           {"startStep", decimal(start)},
           {"count", count},
           {"waitSteps", decimal(skipped)},
           {"fingerprint", traced.deterministicStateFingerprint()},
           {"parity", "passed"},
           {"history",
            "fresh initialization; no suppliers from prior window"}});
      ++wi;
    };
    if (wi < windows.size() &&
        windows[wi].value("scenario", std::string()) == "initialization")
      window(windows[wi]);
    wait([&]() { return ram(traced, 0xc00c) == 1 && commit(traced) >= 2; });
    Json states = Json::array();
    for (auto &scenario : scenarios) {
      if (!scenario.at("frames").is_number_integer() ||
          scenario["frames"] < 1 || scenario["frames"] > 4096 ||
          !scenario.at("mask").is_number_integer() || scenario["mask"] < 0 ||
          scenario["mask"] > 255)
        throw std::invalid_argument("invalid fixture input/frame bounds");
      auto frames = scenario.at("frames").get<unsigned>();
      totalFrames += frames;
      if (totalFrames > 4096)
        throw std::runtime_error("scenario frame bound");
      auto mask = scenario.at("mask").get<uint8_t>();
      input(traced, mask);
      input(plain, mask);
      auto begin = commit(traced);
      auto end = uint16_t(begin + frames);
      if (wi < windows.size() && windows[wi]["scenario"] == scenario["name"]) {
        auto at = windows[wi].value("atFrame", Json(nullptr));
        auto before = at.is_null() ? uint16_t(end - 1)
                                   : uint16_t(begin + at.get<unsigned>() - 1);
        wait([&]() { return commit(traced) >= before; });
        window(windows[wi]);
      }
      wait([&]() { return commit(traced) >= end; });
      Json s = {{"name", scenario["name"]}, {"commit", commit(traced)}};
      for (auto [name, address] :
           std::initializer_list<std::pair<const char *, uint16_t>>{
               {"x", 0xc000},
               {"y", 0xc001},
               {"picked", 0xc002},
               {"score", 0xc003},
               {"won", 0xc004},
               {"pose", 0xc005},
               {"bank", 0xc012},
               {"blocked", 0xc020},
               {"collected", 0xc021},
               {"victory", 0xc022}})
        s[name] = ram(traced, address);
      states.push_back(s);
      if (traced.deterministicStateFingerprint() !=
          plain.deterministicStateFingerprint())
        throw std::runtime_error("scenario parity failed");
    }
    if (wi != windows.size())
      throw std::runtime_error("missed fixture trigger");
    if (steps > omitted)
      report["intentionalOmissions"].push_back(
          {{"fromStep", decimal(omitted)},
           {"toStep", decimal(steps)},
           {"reason", "intentional untraced tail"}});
    for (auto &omission : report["intentionalOmissions"])
      omission["captureId"] = report["windows"].empty()
                                  ? Json("no-window")
                                  : report["windows"][0]["id"];
    auto captured = aggregate.document();
    captured["captureWindows"] = {
        {"version", 1},
        {"windows", report["windows"]},
        {"intentionalOmissions", report["intentionalOmissions"]}};
    Project::write(out / "capture.json", captured);
    auto analyzed = analyzeProject(captured);
    Project::write(out / "analyzed.json", analyzed);
    auto viewer = Project::load(out / "analyzed.json");
    exportHtml(viewer, out / "graph.html");
    report["states"] = states;
    report["ticks"] = ticks;
    report["steps"] = decimal(steps);
    report["reservedAnalysisBytes"] = decimal(budget->used.load());
    report["frames"] = decimal(totalFrames);
    report["romSha256"] = digest(rom);
    report["status"] = "passed";
    Project::write(out / "capture-report.json", report);
    std::cout << report.dump() << '\n';
    return 0;
  } catch (const std::exception &e) {
    report["error"] = e.what();
    if (argc == 5)
      Project::write(std::filesystem::path(argv[4]) / "capture-report.json",
                     report);
    std::cerr << report.dump() << '\n';
    return 1;
  }
}
