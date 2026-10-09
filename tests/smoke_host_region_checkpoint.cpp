#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/SaveState.hpp"
#include "space/Session.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <unistd.h>

namespace {
template<class Core> void exercise(const std::filesystem::path& path, bool gameboy)
{
    Core machine;
    std::vector<std::uint8_t> rom(0x8000, 0);
    machine.loadRom(rom);
    auto& host = machine.modHost();
    const auto id = host.createRegion("inventory", 32, 0x4000, 2);
    auto bytes = host.region(id);
    bytes[0] = 42;
    machine.save_state(path);
    const auto saved = BMMQ::SaveStateReader::read(path);
    const auto original = machine.deterministicStateFingerprint();
    bytes[0] = 91;
    assert(machine.deterministicStateFingerprint() != original);
    machine.runtimeContext().write8(0xc000, 7);
    machine.load_state(path);
    assert(bytes.data() == host.region(id).data());
    assert(bytes[0] == 42);
    assert(machine.deterministicStateFingerprint() == original);

    const auto reject = [&](BMMQ::SaveStateFile corrupt) {
        for (auto& chunk : corrupt.chunks) chunk.size = chunk.data.size();
        BMMQ::SaveStateReader::write(corrupt, path);
        bytes[0] = 88;
        machine.runtimeContext().write8(0xc000, 9);
        const auto before = machine.deterministicStateFingerprint();
        bool failed = false;
        try { machine.load_state(path); } catch (const std::exception&) { failed = true; }
        assert(failed);
        assert(bytes[0] == 88);
        assert(machine.deterministicStateFingerprint() == before);
    };
    auto corrupt = saved;
    auto find = [](auto& state, const char* name) -> auto& {
        auto it = std::find_if(state.chunks.begin(), state.chunks.end(),
            [&](auto& chunk) { return chunk.name == name; });
        assert(it != state.chunks.end());
        return *it;
    };
    // Valid outer checksum cannot legitimize a different host layout or truncated device state.
    find(corrupt, "time.host-regions").data[12] ^= 1;
    reject(corrupt);
    corrupt = saved;
    find(corrupt, "time.host-regions").data.pop_back();
    reject(corrupt);
    corrupt = saved;
    find(corrupt, gameboy ? "gb.cpu" : "gg.cpu").data.clear();
    reject(corrupt);
    corrupt = saved;
    std::erase_if(corrupt.chunks, [](auto& chunk) { return chunk.name == "time.host-regions"; });
    reject(corrupt);
    corrupt = saved;
    corrupt.chunks.push_back(find(corrupt, "time.host-regions"));
    reject(corrupt);

    // Legacy machine files are loadable only when no configured host regions need restoring.
    Core empty;
    empty.loadRom(rom);
    empty.save_state(path);
    auto legacy = BMMQ::SaveStateReader::read(path);
    std::erase_if(legacy.chunks, [](auto& chunk) { return chunk.name == "time.host-regions"; });
    BMMQ::SaveStateReader::write(legacy, path);
    empty.load_state(path);
    reject(legacy);

    // Paired S.P.A.C.E. preflight candidates retain the configured host layout.
    const auto paired = std::filesystem::path(path.string() + ".space");
    {
        BMMQ::Space::Session session(machine, rom);
        bytes[0] = 73;
        session.checkpoint(paired);
        bytes[0] = 74;
        session.step();
        session.restore(paired);
        assert(bytes[0] == 73);
    }
    std::filesystem::remove_all(paired);
    machine.loadRom(rom);
    assert(bytes.data() == host.region(id).data());
    assert(std::all_of(bytes.begin(), bytes.end(), [](auto value) { return value == 0; }));
    std::filesystem::remove(path);
}
}
int main()
{
    const auto path = std::filesystem::temp_directory_path() /
        ("time-host-checkpoint-" + std::to_string(getpid()) + ".ptss");
    exercise<GB::GameBoyMachine>(path, true);
    exercise<BMMQ::GameGearMachine>(path, false);

    BMMQ::Modding::ModHost host;
    const auto id = host.createRegion("inventário", 1);
    const std::vector<std::uint8_t> schema1{1,0,0,0,1,0,0,0,1,0,0,0,37};
    assert(host.importState(schema1));
    assert(host.region(id)[0] == 37);
    assert(!host.importState(std::vector<std::uint8_t>{2}));
    assert(host.region(id)[0] == 37);
    auto prepared = host.prepareState(host.exportState());
    assert(host.createRegion("new", 1));
    bool stale = false;
    try { host.commitState(std::move(prepared)); } catch (const std::invalid_argument&) { stale = true; }
    assert(stale && host.region(id)[0] == 37);
}
