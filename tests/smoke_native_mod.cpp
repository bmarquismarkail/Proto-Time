#include "machine/modding/NativeMod.hpp"
#include <cassert>
#include <thread>

int main(int argc, char** argv) {
    using namespace BMMQ::Modding;
    assert(argc == 7);
    auto host = std::make_shared<ModHost>();
    const auto pool = host->createRegion("fixture:pool", 4);
    const auto other = host->createRegion("other:pool", 4);
    assert(host->addSymbol("Target", {2, 0x4000}));
    LoadedMod metadata{"fixture", "1", 0, {{"pool", pool}}, argv[1]};
    auto mod = NativeMod::load(metadata, host);
    TimeModCallV1 call{sizeof(TimeModCallV1), 1, 0, 0, 0};
    assert(mod->invoke(call) && call.result == 1 && host->region(pool)[0] == 1);
    call.argument = UINT64_MAX;
    assert(!mod->invoke(call));
    call.hook_id = 2;
    assert(mod->invoke(call) && call.result == 0x24000);
    call.hook_id = 4; call.argument = other;
    assert(!mod->invoke(call));
    auto state = mod->save();
    assert(state.bytes == std::vector<std::uint8_t>{1});
    assert(mod->reset());
    call.hook_id = 3;
    assert(mod->invoke(call) && call.result == 0);
    assert(mod->restore(state));
    assert(mod->invoke(call) && call.result == 1);
    state.schema++;
    assert(!mod->restore(state));
    state.schema--; state.version = "wrong";
    assert(!mod->restore(state));
    state.version = "1"; state.bytes.push_back(0);
    assert(!mod->restore(state));
    assert(mod->invoke(call) && call.result == 1);
    bool rejected = false;
    std::thread wrongThread([&] { try { (void)mod->reset(); } catch (...) { rejected = true; } });
    wrongThread.join();
    assert(rejected);
    call.struct_size = 0;
    assert(!mod->invoke(call));
    auto fails = [&](LoadedMod bad) {
        try { auto ignored = NativeMod::load(bad, host); return false; }
        catch (const std::exception&) { return true; }
    };
    auto bad = metadata; bad.version = "wrong";
    assert(fails(bad));
    bad = metadata; bad.nativeModule = argv[2]; assert(fails(bad));
    bad.nativeModule = argv[3]; assert(fails(bad));
    for (int i = 4; i < argc; ++i) { bad.nativeModule = argv[i]; assert(fails(bad)); }
    assert(host->region(pool)[3] == 42); // Partial construction was destroyed.
    host->region(pool)[3] = 0;
    std::weak_ptr<ModHost> lifetime = host;
    host.reset();
    assert(!lifetime.expired());
    auto retained = lifetime.lock();
    mod.reset();
    assert(retained->region(pool)[3] == 42); // Host still valid during destroy.
    retained.reset();
    assert(lifetime.expired());
}
