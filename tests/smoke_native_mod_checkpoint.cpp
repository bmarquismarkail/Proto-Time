#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/modding/NativeMod.hpp"
#include "machine/SaveState.hpp"
#include "space/Capture.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace {
struct ObserverCopyBudget {
    std::shared_ptr<int> copies;
    explicit ObserverCopyBudget(std::shared_ptr<int> value) : copies(std::move(value)) {}
    ObserverCopyBudget(const ObserverCopyBudget& other) : copies(other.copies) {
        if (*copies >= 0 && ++*copies > 1) throw std::runtime_error("observer copy budget exhausted");
    }
    void operator()(std::uint16_t, std::uint8_t) const {}
};
template<class Core> void exercise(const char* library, const std::filesystem::path& path)
{
    using namespace BMMQ::Modding;
    Core machine;
    std::vector<std::uint8_t> rom(0x8000, 0);
    rom[0x200] = 0xc9;
    machine.loadRom(rom);
    auto& host = machine.modHost();
    const auto pool = host.createRegion("fixture:pool", 4);
    LoadedMod metadata{"fixture", "1", 0, {{"pool", pool}}, library, {}};
    auto hook = NativeMod::load(metadata, host);
    auto* stableHook = hook.get();
    if constexpr (std::is_same_v<Core, GB::GameBoyMachine>)
        assert(machine.installNativeTrampoline(0x200, std::move(hook), 1));
    else assert(machine.installNativeTrampoline(0x200, 0, std::move(hook), 1));
    // A module owned by tooling, without an installed trampoline, participates too.
    auto observer = NativeMod::load(metadata, host);
    BMMQ::Space::Capture capture;
    bool captureRejected = false;
    try { machine.setAnalysisCapture(&capture); } catch (const std::invalid_argument&) { captureRejected = true; }
    assert(captureRejected);
    TimeModCallV1 call{sizeof(TimeModCallV1), 1, 0, 0, 0};
    assert(stableHook->invoke(call));
    assert(observer->invoke(call));
    assert(observer->invoke(call));
    machine.save_state(path);
    const auto saved = BMMQ::SaveStateReader::read(path);
    const auto fingerprint = machine.deterministicStateFingerprint();
    assert(stableHook->invoke(call));
    assert(observer->invoke(call));
    const auto generation = machine.observationGeneration();
    for (unsigned i = 0; i < 32; ++i) machine.load_state(path);
    assert(machine.observationGeneration() > generation);
    assert(machine.deterministicStateFingerprint() == fingerprint);
    call.hook_id = 3;
    assert(stableHook->invoke(call) && call.result == 1);
    assert(observer->invoke(call) && call.result == 2);
    assert(host.region(pool)[0] == 3);
    assert(host.region(pool)[3] == 0); // Retired destroy callbacks wrote only isolated hosts.

    const auto rejects = [&](auto state) {
        for (auto& chunk : state.chunks) chunk.size = chunk.data.size();
        BMMQ::SaveStateReader::write(state, path);
        const auto before = machine.deterministicStateFingerprint();
        bool failed = false;
        try { machine.load_state(path); } catch (const std::exception&) { failed = true; }
        assert(failed);
        assert(machine.deterministicStateFingerprint() == before);
    };
    auto corrupt = saved;
    auto native = std::find_if(corrupt.chunks.begin(), corrupt.chunks.end(),
        [](auto& chunk) { return chunk.name == "time.native-mods"; });
    assert(native != corrupt.chunks.end());
    native->data[48] = 254; // Restore mutates its private state AND host, then fails.
    rejects(corrupt);
    corrupt = saved;
    native = std::find_if(corrupt.chunks.begin(), corrupt.chunks.end(),
        [](auto& chunk) { return chunk.name == "time.native-mods"; });
    native->data[8] ^= 1; // Exact module/binding identity differs.
    rejects(corrupt);
    corrupt = saved;
    std::erase_if(corrupt.chunks, [](auto& chunk) { return chunk.name == "time.native-mods"; });
    rejects(corrupt);

    if constexpr (std::is_same_v<Core, GB::GameBoyMachine>) {
        auto& bus = dynamic_cast<GB::GameBoyMemoryMap&>(machine.executionMemory().backingStore());
        auto copies = std::make_shared<int>(-1);
        bus.setWriteObserver(ObserverCopyBudget(copies));
        BMMQ::SaveStateReader::write(saved, path);
        *copies = 1; // Even the staging copy fails: all live state must remain untouched.
        const auto before = machine.deterministicStateFingerprint();
        bool rejected = false;
        try { machine.load_state(path); } catch (const std::runtime_error&) { rejected = true; }
        assert(rejected && machine.deterministicStateFingerprint() == before);
        *copies = 0;
        machine.load_state(path); // One staging copy succeeds; publication must not copy again.
        assert(*copies == 1);
        *copies = -1;
    }
    machine.runtimeContext().writeRegister16("PC", 0x200);
    machine.runtimeContext().writeRegister16("SP", 0xc100);
    machine.runtimeContext().writeRegister16("HL", 0);
    machine.runtimeContext().write8(0xc100, 3);
    machine.runtimeContext().write8(0xc101, 1);
    machine.step();
    assert(machine.runtimeContext().readRegister16("PC") == 0x103);
    assert(machine.runtimeContext().readRegister16("HL") == 4);
    assert(stableHook->save().bytes[0] == 2);

    bool wrongLane = false;
    std::thread other([&] {
        try { (void)NativeMod::saveCohort(host); } catch (...) { wrongLane = true; }
    });
    other.join();
    assert(wrongLane);

    auto prepared = NativeMod::prepareReset(host);
    observer.reset();
    bool stale = false;
    try { prepared.commit(); } catch (const std::invalid_argument&) { stale = true; }
    assert(stale); // No borrowed pointer is dereferenced after participant destruction.

    auto changed = NativeMod::prepareReset(host);
    call.hook_id = 1;
    assert(stableHook->invoke(call));
    const auto afterEdit = machine.deterministicStateFingerprint();
    stale = false;
    try { changed.commit(); } catch (const std::invalid_argument&) { stale = true; }
    assert(stale && machine.deterministicStateFingerprint() == afterEdit);

    auto state = stableHook->save();
    state.bytes[0] = 253;
    assert(stableHook->restore(state));
    const auto beforeReset = machine.deterministicStateFingerprint();
    bool resetFailed = false;
    try { machine.resetModState(); } catch (const std::exception&) { resetFailed = true; }
    assert(resetFailed);
    assert(machine.deterministicStateFingerprint() == beforeReset);
    resetFailed = false;
    try { machine.loadRom(rom); } catch (const std::exception&) { resetFailed = true; }
    assert(resetFailed && machine.deterministicStateFingerprint() == beforeReset);
    state.bytes[0] = 2;
    assert(stableHook->restore(state));
    machine.resetModState();
    assert(stableHook->save().bytes[0] == 0);
    assert(std::all_of(host.region(pool).begin(), host.region(pool).end(), [](auto b) { return b == 0; }));
    Core observerCore;
    observerCore.loadRom(rom);
    auto observerPool = observerCore.modHost().createRegion("fixture:pool", 4);
    metadata.regions = {{"pool", observerPool}};
    auto observerOnly = NativeMod::load(metadata, observerCore.modHost());
    captureRejected = false;
    try { observerCore.setAnalysisCapture(&capture); }
    catch (const std::invalid_argument&) { captureRejected = true; }
    assert(captureRejected);
    std::filesystem::remove(path);
}
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    auto path = std::filesystem::temp_directory_path() / ("time-native-checkpoint-" + std::to_string(getpid()) + ".ptss");
    exercise<GB::GameBoyMachine>(argv[1], path);
    exercise<BMMQ::GameGearMachine>(argv[1], path);

    // Loaded-image binding survives path changes; a changed file cannot validate a restore.
    using namespace BMMQ::Modding;
    const auto image = path.string() + ".so";
    std::filesystem::copy_file(argv[1], image);
    ModHost host;
    const auto pool = host.createRegion("fixture:pool", 4);
    LoadedMod metadata{"fixture", "1", 0, {{"pool", pool}}, image, {}};
    auto module = NativeMod::load(metadata, host);
    assert(NativeMod::hasCohort(host));
    const auto modules = NativeMod::saveCohort(host), regions = host.exportState();
    { std::ofstream output(image, std::ios::binary | std::ios::app); output.put(0); }
    bool rejected = false;
    try { (void)NativeMod::prepareCohort(host, modules, regions); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected && host.exportState() == regions && NativeMod::saveCohort(host) == modules);
    module.reset();
    std::filesystem::remove(image);
    auto owner = std::make_unique<ModHost>();
    auto expired = NativeMod::prepareReset(*owner);
    owner.reset();
    rejected = false;
    try { expired.commit(); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::optional<ModHost> borrowedHost;
    borrowedHost.emplace();
    const auto borrowedPool = borrowedHost->createRegion("fixture:pool", 4);
    metadata.nativeModule = argv[1];
    metadata.regions = {{"pool", borrowedPool}};
    auto borrowed = NativeMod::load(metadata, *borrowedHost);
    borrowedHost.reset();
    TimeModCallV1 call{sizeof(TimeModCallV1), 1, 0, 0, 0};
    rejected = false;
    try { (void)borrowed->invoke(call); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    borrowedHost.emplace(); // Reuses the address, with a different lifetime.
    assert(!NativeMod::hasCohort(*borrowedHost));
    borrowed.reset(); // Destroy callback cannot access the expired host.
}
