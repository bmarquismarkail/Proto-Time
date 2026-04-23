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

    auto emitOutA = [&](uint8_t port) {
        emit8(0xD3u);
        emit8(port);
    };
    auto emitRegWrite = [&](uint8_t reg, uint8_t value) {
        emit8(0x3Eu);
        emit8(value);
        emitOutA(0xBFu);
        emit8(0x3Eu);
        emit8(static_cast<uint8_t>(0x80u | (reg & 0x0Fu)));
        emitOutA(0xBFu);
    };
    auto emitSetVramWriteAddress = [&](uint16_t address) {
        emit8(0x3Eu);
        emit8(static_cast<uint8_t>(address & 0x00FFu));
        emitOutA(0xBFu);
        emit8(0x3Eu);
        emit8(static_cast<uint8_t>(0x40u | ((address >> 8) & 0x3Fu)));
        emitOutA(0xBFu);
    };
    auto emitRel8 = [&](int opcode, std::size_t target) {
        emit8(static_cast<uint8_t>(opcode));
        const auto baseAfterOperand = static_cast<int>(pc + 1u);
        const auto delta = static_cast<int>(target) - baseAfterOperand;
        emit8(static_cast<uint8_t>(static_cast<int8_t>(delta)));
    };

    emitRegWrite(1u, 0x40u);
    emitRegWrite(5u, 0xFFu);
    emitRegWrite(6u, 0xFFu);
    emitRegWrite(7u, 0x00u);
    emit8(0x3Eu); emit8(0x22u); emitOutA(0xBFu);
    emit8(0x3Eu); emit8(0xC0u); emitOutA(0xBFu);
    emit8(0x3Eu); emit8(0x0Fu); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emitSetVramWriteAddress(0x2020u);
    emit8(0x06u); emit8(0x08u);
    const auto fillTileLoop = pc;
    emit8(0x3Eu); emit8(0xFFu); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emitRel8(0x10u, fillTileLoop);

    emitSetVramWriteAddress(0x3F00u);
    emit8(0x3Eu); emit8(24u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0xD0u); emitOutA(0xBEu);
    emitSetVramWriteAddress(0x3F80u);
    emit8(0x3Eu); emit8(24u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x01u); emitOutA(0xBEu);

    emitLoadHlAndByte(0xFE00u, 24u);
    emitLoadHlAndByte(0xFE01u, 24u);
    emitLoadHlAndByte(0xFE02u, 0x01u);
    emitLoadHlAndByte(0xFE03u, 0x00u);

    const auto idleLoop = pc;
    emitRel8(0x18u, idleLoop);

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
    const auto spritePixel = frontend->lastFrame()->pixels[24u * 160u + 24u];
    const auto backgroundPixel = frontend->lastFrame()->pixels[24u * 160u + 8u];
    assert(spritePixel != backgroundPixel);
    assert(frontend->stats().videoEvents >= 1u);
    assert(frontend->stats().framesPrepared >= 1u);

    machine.pluginManager().shutdown(machine.mutableView());
    return 0;
}
