#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "cores/gamegear/GameGearMachine.hpp"
#include "machine/plugins/SdlFrontendPlugin.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"

namespace {

std::vector<uint8_t> makeFrontendProofRom()
{
    std::vector<uint8_t> rom(0x4000u, 0x00u);
    std::size_t pc = 0u;
    auto emit8 = [&](uint8_t value) {
        rom[pc++] = value;
    };
    auto emit16 = [&](uint16_t value) {
        emit8(static_cast<uint8_t>(value & 0x00FFu));
        emit8(static_cast<uint8_t>((value >> 8) & 0x00FFu));
    };
    auto emitLoadHlAndByte = [&](uint16_t address, uint8_t value) {
        emit8(0x21u);
        emit16(address);
        emit8(0x36u);
        emit8(value);
    };

    emitLoadHlAndByte(0xFF40u, 0x91u);
    emitLoadHlAndByte(0xFF47u, 0xE4u);
    emit8(0x21u);
    emit16(0x8000u);
    for (int row = 0; row < 8; ++row) {
        emit8(0x36u);
        emit8(0xFFu);
        emit8(0x23u);
        emit8(0x36u);
        emit8(0x00u);
        emit8(0x23u);
    }
    emit8(0x21u);
    emit16(0x9800u);
    emit8(0x36u);
    emit8(0x00u);
    emit8(0xC3u);
    emit16(static_cast<uint16_t>(pc));

    return rom;
}

bool frameHasVisiblePixels(const BMMQ::SdlFrameBuffer& frame)
{
    for (const auto pixel : frame.pixels) {
        if (pixel != 0xFF000000u) {
            return true;
        }
    }
    return false;
}

}

int main(int argc, char** argv)
{
#if defined(__unix__) || defined(__APPLE__)
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif

    BMMQ::SdlFrontendConfig config;
    config.windowTitle = "Proto-Time SDL Game Gear Smoke";
    config.windowScale = 2u;
    config.frameWidth = 160;
    config.frameHeight = 144;
    config.enableAudio = false;
    config.autoInitializeBackend = false;
    config.autoPresentOnVideoEvent = false;

    BMMQ::GameGearMachine machine;
    machine.loadRom(makeFrontendProofRom());

    const auto executablePath = (argc > 0 && argv != nullptr)
        ? std::filesystem::path(argv[0])
        : std::filesystem::path("time-smoke-sdl-frontend-gamegear");
    auto frontendPlugin = BMMQ::loadSdlFrontendPlugin(
        BMMQ::defaultSdlFrontendPluginPath(executablePath),
        config);
    auto* frontend = frontendPlugin.get();
    machine.pluginManager().add(std::move(frontendPlugin));
    machine.pluginManager().initialize(machine.mutableView());

    bool sawFrame = false;
    for (int i = 0; i < 20000; ++i) {
        machine.step();
        if ((i % 16) == 0) {
            machine.serviceInput();
            (void)frontend->serviceFrontend();
        }
        if (frontend->lastFrame().has_value() && frameHasVisiblePixels(*frontend->lastFrame())) {
            sawFrame = true;
            break;
        }
    }

    assert(sawFrame);
    assert(frontend->lastVideoState().has_value());
    assert(frontend->lastVideoState()->lcdc == 0x91u);
    assert(frontend->lastFrame().has_value());
    assert(frontend->lastFrame()->width == 160);
    assert(frontend->lastFrame()->height == 144);
    assert(frameHasVisiblePixels(*frontend->lastFrame()));

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
