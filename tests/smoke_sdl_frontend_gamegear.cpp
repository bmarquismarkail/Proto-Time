#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <unordered_set>
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
    emit8(0x23u);
    emit8(0x36u);
    emit8(0x01u);

    emit8(0x21u);
    emit16(0x8010u);
    for (int row = 0; row < 8; ++row) {
        emit8(0x36u);
        emit8(0xFFu);
        emit8(0x23u);
        emit8(0x36u);
        emit8(0xFFu);
        emit8(0x23u);
    }

    emit8(0x21u);
    emit16(0xC000u);
    const auto inputLoopAddress = static_cast<uint16_t>(pc);
    emit8(0xDBu);
    emit8(0xDCu);
    emit8(0x77u);
    emit8(0xE6u);
    emit8(0x10u);
    emit8(0xCAu);
    const auto pressedJumpPatch = pc;
    emit16(0x0000u);

    emit8(0x21u);
    emit16(0x9800u);
    emit8(0x36u);
    emit8(0x00u);
    emit8(0xC3u);
    emit16(inputLoopAddress);

    const auto pressedPath = static_cast<uint16_t>(pc);
    rom[pressedJumpPatch] = static_cast<uint8_t>(pressedPath & 0x00FFu);
    rom[pressedJumpPatch + 1u] = static_cast<uint8_t>((pressedPath >> 8) & 0x00FFu);
    emit8(0x21u);
    emit16(0x9800u);
    emit8(0x36u);
    emit8(0x01u);
    emit8(0xC3u);
    emit16(inputLoopAddress);

    return rom;
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

    for (int i = 0; i < 20000; ++i) {
        machine.step();
        if ((i % 16) == 0) {
            machine.serviceInput();
            (void)frontend->serviceFrontend();
        }
    }

    assert(frontend->lastVideoDebugModel().has_value());
    assert(frontend->lastVideoDebugModel()->displayEnabled);
    assert(frontend->lastFrame().has_value());
    std::unordered_set<std::uint32_t> frameColors(
        frontend->lastFrame()->pixels.begin(),
        frontend->lastFrame()->pixels.end());
    assert(frameColors.size() > 1u);
    assert(frontend->stats().videoEvents >= 1u);
    assert(frontend->stats().framesPrepared >= 1u);

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
