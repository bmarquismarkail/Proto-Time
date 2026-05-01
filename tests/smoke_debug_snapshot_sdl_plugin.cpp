#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/DebugSnapshotService.hpp"
#include "machine/DebugSnapshotTypes.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"

namespace {

// Minimal idle-loop Game Gear ROM — halts after VDP init.
std::vector<uint8_t> makeMinimalGGRom()
{
    std::vector<uint8_t> rom(0x4000u, 0x00u);
    // JR 0 (infinite loop at 0x0000)
    rom[0] = 0x18u;  // JR
    rom[1] = 0xFEu;  // offset -2 → loops back to 0x0000
    return rom;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    // -----------------------------------------------------------------------
    // Test 1: Injection and retrieval — no machine needed
    // -----------------------------------------------------------------------
    {
        BMMQ::SdlFrontendConfig config;
        config.autoInitializeBackend = false;
        config.enableAudio = false;
        config.enableVideo = false;

        const auto executablePath = (argc > 0 && argv != nullptr)
            ? std::filesystem::path(argv[0])
            : std::filesystem::path("time-smoke-debug-snapshot-sdl-plugin");
        auto plugin = BMMQ::loadSdlFrontendPlugin(
            BMMQ::defaultSdlFrontendPluginPath(executablePath), config);

        // Starts null
        assert(plugin->debugSnapshotService() == nullptr);

        // Inject a service
        BMMQ::DebugSnapshotService svc;
        plugin->setDebugSnapshotService(&svc);
        assert(plugin->debugSnapshotService() == &svc);

        // Clear it
        plugin->setDebugSnapshotService(nullptr);
        assert(plugin->debugSnapshotService() == nullptr);
    }

    // -----------------------------------------------------------------------
    // Test 2: With emulation — service receives video snapshots via VBlank
    // -----------------------------------------------------------------------
    {
        BMMQ::SdlFrontendConfig config;
        config.windowTitle = "Debug Snapshot SDL Smoke";
        config.frameWidth = 160;
        config.frameHeight = 144;
        config.enableAudio = false;
        config.autoInitializeBackend = false;
        config.autoPresentOnVideoEvent = false;

        BMMQ::GameGearMachine machine;
        machine.loadRom(makeMinimalGGRom());

        const auto executablePath = (argc > 0 && argv != nullptr)
            ? std::filesystem::path(argv[0])
            : std::filesystem::path("time-smoke-debug-snapshot-sdl-plugin");
        auto frontendPlugin = BMMQ::loadSdlFrontendPlugin(
            BMMQ::defaultSdlFrontendPluginPath(executablePath), config);
        auto* frontend = frontendPlugin.get();

        BMMQ::DebugSnapshotService svc;
        frontend->setDebugSnapshotService(&svc);
        assert(frontend->debugSnapshotService() == &svc);

        machine.pluginManager().add(std::move(frontendPlugin));
        machine.pluginManager().initialize(machine.mutableView());

        // Step for a few VBlank periods (one VBlank every ~70000 T-states, so
        // 150 000 steps should include at least 2 VBlanks).
        for (int i = 0; i < 150000; ++i) {
            machine.step();
        }

        // After stepping, the emulation thread has submitted video captures.
        // Drain them by calling serviceFrontend() which calls tryConsumeVideo().
        (void)frontend->serviceFrontend();

        // The service should have seen at least one video submission.
        const auto s = svc.stats();
        assert(s.videoSubmissions >= 1u);

        // The plugin should now have a cached video debug model from the drain.
        // (Some may have been submitted and consumed.)
        assert(s.videoConsumptions <= s.videoSubmissions);
        assert(s.videoOverflows == 0u || s.videoOverflows < s.videoSubmissions);

        machine.pluginManager().shutdown(machine.mutableView());
    }

    return 0;
}
