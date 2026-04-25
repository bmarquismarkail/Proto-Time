#include "cores/gamegear/GameGearMachine.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearVDP.hpp"
#include "cores/gamegear/Z80Interpreter.hpp"
#include "machine/InputTypes.hpp"
#include "machine/VideoDebugModel.hpp"
#include "machine/plugins/input/InputPlugin.hpp"
#include "machine/plugins/IoPlugin.hpp"
#include "machine/plugins/PluginManager.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>
#include <vector>

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
        emit8(0x21u); // LD HL,nn
        emit16(address);
        emit8(0x36u); // LD (HL),n
        emit8(value);
    };

    auto emitOutA = [&](uint8_t port) {
        emit8(0xD3u); // OUT (n),A
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

    emitRegWrite(1u, 0x60u); // display enable + frame IRQ enable
    emitRegWrite(5u, 0xFFu); // SAT base 0x3F00
    emitRegWrite(6u, 0xFFu); // sprite generator base 0x2000
    emitRegWrite(7u, 0x00u); // backdrop color code 0

    emit8(0x3Eu); emit8(0x22u); emitOutA(0xBFu); // CRAM palette1 color1 addr
    emit8(0x3Eu); emit8(0xC0u); emitOutA(0xBFu);
    emit8(0x3Eu); emit8(0x0Fu); emitOutA(0xBEu); // red
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);

    emitSetVramWriteAddress(0x2020u); // tile 1 pattern data
    emit8(0x06u); emit8(0x08u); // LD B,8
    const auto fillTileLoop = pc;
    emit8(0x3Eu); emit8(0xFFu); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x00u); emitOutA(0xBEu);
    emitRel8(0x10u, fillTileLoop); // DJNZ fillTileLoop

    emitSetVramWriteAddress(0x3F00u); // SAT y table
    emit8(0x3Eu); emit8(47u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0xD0u); emitOutA(0xBEu);
    emitSetVramWriteAddress(0x3F80u); // SAT x/tile table
    emit8(0x3Eu); emit8(72u); emitOutA(0xBEu);
    emit8(0x3Eu); emit8(0x01u); emitOutA(0xBEu);

    emitLoadHlAndByte(0xFE00u, 47u); // compat SAT shim y
    emitLoadHlAndByte(0xFE01u, 72u); // compat SAT shim x
    emitLoadHlAndByte(0xFE02u, 0x01u); // compat SAT shim tile
    emitLoadHlAndByte(0xC000u, 0x00u); // observed input state byte

    const std::size_t inputLoop = pc;
    emit8(0xDBu); emit8(0xDCu); // IN A,(0xDC)
    emitLoadHlAndByte(0xC000u, 0x00u); // default
    emit8(0x21u); emit16(0xC000u); // LD HL,0xC000
    emit8(0x77u); // LD (HL),A
    emit8(0xE6u); emit8(0x10u); // AND 0x10
    emit8(0x20u); // JR NZ,notPressed
    const auto notPressedPatch = pc;
    emit8(0x00u);
    emitLoadHlAndByte(0xFE01u, 88u); // pressed -> move sprite right
    emitRel8(0x18u, inputLoop); // JR inputLoop
    const auto notPressed = pc;
    rom[notPressedPatch] = static_cast<uint8_t>(static_cast<int8_t>(
        static_cast<int>(notPressed) - static_cast<int>(notPressedPatch + 1u)));
    emitLoadHlAndByte(0xFE01u, 72u); // not pressed -> left position
    emitRel8(0x18u, inputLoop); // JR inputLoop

    return rom;
}

std::vector<uint8_t> makeDisplayOnlyRom()
{
    std::vector<uint8_t> rom(0x4000u, 0x00u);
    std::size_t pc = 0u;
    auto emit8 = [&](uint8_t value) {
        rom[pc++] = value;
    };
    auto emitOutA = [&](uint8_t port) {
        emit8(0xD3u);
        emit8(port);
    };

    emit8(0x3Eu);
    emit8(0x40u);
    emitOutA(0xBFu);
    emit8(0x3Eu);
    emit8(0x81u);
    emitOutA(0xBFu);
    const auto idleLoop = pc;
    emit8(0x18u);
    emit8(static_cast<uint8_t>(static_cast<int8_t>(static_cast<int>(idleLoop) - static_cast<int>(pc + 1u))));
    return rom;
}

bool stepUntil(BMMQ::GameGearMachine& machine, int maxSteps, auto&& predicate)
{
    for (int i = 0; i < maxSteps; ++i) {
        machine.step();
        if (predicate()) {
            return true;
        }
    }
    return false;
}

class FixedInputPlugin final : public BMMQ::IDigitalInputSourcePlugin {
public:
    explicit FixedInputPlugin(BMMQ::InputButtonMask value)
        : value_(value) {}

    BMMQ::InputPluginCapabilities capabilities() const noexcept override {
        return BMMQ::InputPluginCapabilities{
            true,
            false,
            true,
            true,
            false,
            true,
            false,
            false,
            false,
            true
        };
    }

    std::string_view name() const noexcept override {
        return "fixed-gamegear-input";
    }

    bool open() override {
        return true;
    }

    void close() noexcept override {
    }

    std::string_view lastError() const noexcept override {
        return {};
    }

    std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override {
        return value_;
    }

private:
    BMMQ::InputButtonMask value_ = 0u;
};

class FirstScanlinePlugin final : public BMMQ::IVideoPlugin {
public:
    std::optional<BMMQ::MachineEvent> firstScanlineEvent;

    std::string_view id() const override {
        return "test.gamegear.first-scanline";
    }

    void onVideoEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView&) override {
        if (event.type == BMMQ::MachineEventType::VideoScanlineReady &&
            !firstScanlineEvent.has_value()) {
            firstScanlineEvent = event;
        }
    }
};

}

int main() {
    GameGearVDP vdp;
    GameGearMemoryMap ioMemory;
    ioMemory.setVdp(&vdp);
    vdp.reset();
    ioMemory.writeIoPort(0xBFu, 0x34u);
    ioMemory.writeIoPort(0xBFu, 0x81u);
    assert(ioMemory.read(0xFF41u) == 0x34u);
    ioMemory.writeIoPort(0xBFu, 0x10u);
    ioMemory.writeIoPort(0xBFu, 0x40u);
    ioMemory.writeIoPort(0xBEu, 0x12u);
    ioMemory.writeIoPort(0xBEu, 0x34u);
    assert(ioMemory.read(0x8010u) == 0x12u);
    assert(ioMemory.read(0x8011u) == 0x34u);
    ioMemory.writeIoPort(0xBFu, 0x10u);
    ioMemory.writeIoPort(0xBFu, 0x00u);
    assert(ioMemory.readIoPort(0xBEu) == 0x12u);
    assert(ioMemory.readIoPort(0xBEu) == 0x34u);
    ioMemory.writeIoPort(0x81u, 0x78u);
    ioMemory.writeIoPort(0x81u, 0x83u);
    assert(ioMemory.read(0xFF43u) == 0x78u);
    assert(ioMemory.readIoPort(0x7Eu) == 0x00u);
    assert(ioMemory.readIoPort(0x7Fu) == 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x22u);
    ioMemory.writeIoPort(0xBFu, 0xC0u);
    ioMemory.writeIoPort(0xBEu, 0x0Fu);
    auto unchangedCramModel = vdp.buildFrameModel({160, 144});
    assert(unchangedCramModel.argbPixels[0] == unchangedCramModel.argbPixels[1]);
    ioMemory.writeIoPort(0xBEu, 0x00u);
    auto latchedCramModel = vdp.buildFrameModel({160, 144});
    assert(latchedCramModel.argbPixels[0] == unchangedCramModel.argbPixels[0]);
    ioMemory.writeIoPort(0xBFu, 0xFFu);
    ioMemory.writeIoPort(0xBFu, 0x85u); // reg #5 = 0xFF -> SAT base 0x3F00
    ioMemory.writeIoPort(0xBFu, 0xFFu);
    ioMemory.writeIoPort(0xBFu, 0x86u); // reg #6 = 0xFF -> sprite gen base 0x2000
    ioMemory.writeIoPort(0xBFu, 0x40u);
    ioMemory.writeIoPort(0xBFu, 0x81u); // reg #1 = display on
    vdp.step(228u * 193u);
    const auto vblankStatus = ioMemory.readIoPort(0xBFu);
    assert((vblankStatus & 0x80u) != 0u);
    assert((ioMemory.readIoPort(0xBFu) & 0x80u) == 0u);
    ioMemory.writeIoPort(0xBFu, 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x81u);
    ioMemory.writeIoPort(0xBFu, 0x40u);
    ioMemory.writeIoPort(0xBFu, 0x81u);
    ioMemory.writeIoPort(0xBFu, 0x22u);
    ioMemory.writeIoPort(0xBFu, 0xC0u); // CRAM addr 0x22 (palette1 color1)
    ioMemory.writeIoPort(0xBEu, 0x0Fu); // red
    ioMemory.writeIoPort(0xBEu, 0x00u);
    ioMemory.writeIoPort(0xBEu, 0xF0u); // palette1 color2 -> green
    ioMemory.writeIoPort(0xBEu, 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x20u);
    ioMemory.writeIoPort(0xBFu, 0x60u); // VRAM addr 0x2020
    for (int row = 0; row < 8; ++row) {
        ioMemory.writeIoPort(0xBEu, 0xFFu);
        ioMemory.writeIoPort(0xBEu, 0x00u);
        ioMemory.writeIoPort(0xBEu, 0x00u);
        ioMemory.writeIoPort(0xBEu, 0x00u);
    }
    ioMemory.writeIoPort(0xBFu, 0x40u);
    ioMemory.writeIoPort(0xBFu, 0x60u); // VRAM addr 0x2040 tile 2
    for (int row = 0; row < 8; ++row) {
        ioMemory.writeIoPort(0xBEu, 0x00u);
        ioMemory.writeIoPort(0xBEu, 0xFFu);
        ioMemory.writeIoPort(0xBEu, 0x00u);
        ioMemory.writeIoPort(0xBEu, 0x00u);
    }
    ioMemory.writeIoPort(0xBFu, 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x7Fu); // VRAM addr 0x3F00 SAT y-table
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0xD0u);
    ioMemory.writeIoPort(0xBFu, 0x80u);
    ioMemory.writeIoPort(0xBFu, 0x7Fu); // VRAM addr 0x3F80 SAT x/tile
    ioMemory.writeIoPort(0xBEu, 23u);
    ioMemory.writeIoPort(0xBEu, 0x01u);
    ioMemory.writeIoPort(0xBFu, 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x7Fu);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0u);
    ioMemory.writeIoPort(0xBEu, 0xD0u);
    ioMemory.writeIoPort(0xBFu, 0x80u);
    ioMemory.writeIoPort(0xBFu, 0x7Fu);
    for (int i = 0; i < 9; ++i) {
        ioMemory.writeIoPort(0xBEu, 23u);
        ioMemory.writeIoPort(0xBEu, 0x01u);
    }
    for (int i = 0; i < 262; ++i) {
        vdp.step(228u);
        if (vdp.currentScanline() == 2u) {
            break;
        }
    }
    const auto spriteStatus = ioMemory.readIoPort(0xBFu);
    assert((spriteStatus & 0x40u) != 0u);
    assert((spriteStatus & 0x20u) != 0u);
    ioMemory.writeIoPort(0xBFu, 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x7Fu); // restore SAT y-table
    ioMemory.writeIoPort(0xBEu, 47u);
    ioMemory.writeIoPort(0xBEu, 0xD0u);
    ioMemory.writeIoPort(0xBFu, 0x80u);
    ioMemory.writeIoPort(0xBFu, 0x7Fu); // restore SAT x/tile table
    ioMemory.writeIoPort(0xBEu, 72u);
    ioMemory.writeIoPort(0xBEu, 0x01u);
    auto vdpModel = vdp.buildFrameModel({160, 144});
    const auto redSpritePixel = vdpModel.argbPixels[24u * 160u + 24u];
    assert(redSpritePixel != vdpModel.argbPixels[0]);
    const auto lowerPixelBeforeTall = vdpModel.argbPixels[(24u + 12u) * 160u + 24u];
    ioMemory.writeIoPort(0xBFu, 0x42u);
    ioMemory.writeIoPort(0xBFu, 0x81u); // reg #1 = display on + 8x16 sprites
    auto tallSpriteModel = vdp.buildFrameModel({160, 144});
    assert(tallSpriteModel.argbPixels[(24u + 12u) * 160u + 24u] != tallSpriteModel.argbPixels[0]);
    assert(lowerPixelBeforeTall == vdpModel.argbPixels[0]);
    ioMemory.writeIoPort(0xBFu, 0x40u);
    ioMemory.writeIoPort(0xBFu, 0x81u); // back to 8x8 sprites
    ioMemory.writeIoPort(0xBFu, 0x22u);
    ioMemory.writeIoPort(0xBFu, 0xC0u);
    ioMemory.writeIoPort(0xBEu, 0x00u);
    ioMemory.writeIoPort(0xBEu, 0x0Fu); // blue
    auto blueModel = vdp.buildFrameModel({160, 144});
    assert(blueModel.argbPixels[24u * 160u + 24u] != redSpritePixel);
    ioMemory.writeIoPort(0xBFu, 0xFDu);
    ioMemory.writeIoPort(0xBFu, 0x85u); // reg #5 = 0xFD -> SAT base 0x3E00
    auto relocatedSatBlank = vdp.buildFrameModel({160, 144});
    assert(relocatedSatBlank.argbPixels[24u * 160u + 24u] == relocatedSatBlank.argbPixels[0]);
    ioMemory.writeIoPort(0xBFu, 0x00u);
    ioMemory.writeIoPort(0xBFu, 0x7Eu); // VRAM addr 0x3E00 SAT y-table
    ioMemory.writeIoPort(0xBEu, 47u);
    ioMemory.writeIoPort(0xBEu, 0xD0u);
    ioMemory.writeIoPort(0xBFu, 0x80u);
    ioMemory.writeIoPort(0xBFu, 0x7Eu); // VRAM addr 0x3E80 SAT x/tile
    ioMemory.writeIoPort(0xBEu, 72u);
    ioMemory.writeIoPort(0xBEu, 0x01u);
    auto relocatedSatModel = vdp.buildFrameModel({160, 144});
    assert(relocatedSatModel.argbPixels[24u * 160u + 24u] != relocatedSatModel.argbPixels[0]);

    BMMQ::GameGearMachine gg;
    const std::vector<uint8_t> rom = makeFrontendProofRom();
    gg.loadRom(rom);
    assert(stepUntil(gg, 256, [&] {
        return gg.runtimeContext().read8(0xFF41u) == 0x60u;
    }));
    assert(gg.runtimeContext().readRegister16("PC") != 0u);
    gg.runtimeContext().writeRegister16("SP", 0xD123u);
    assert(gg.runtimeContext().readRegister16("SP") == 0xD123u);
    assert(gg.readRegisterPair("SP") == 0xD123u);
    assert(stepUntil(gg, 2048, [&] {
        return gg.runtimeContext().read8(0xFE02u) == 0x01u;
    }));
    assert(gg.runtimeContext().read8(0xFE00u) == 47u);
    assert(gg.runtimeContext().read8(0xFE01u) == 72u);
    assert(gg.runtimeContext().read8(0xFE02u) == 0x01u);
    assert(gg.runtimeContext().read8(0xFF45u) == 0xFFu);
    assert(gg.runtimeContext().read8(0xFF46u) == 0xFFu);
    auto initialModel = gg.videoDebugFrameModel(BMMQ::VideoDebugRenderRequest{
        .frameWidth = 160,
        .frameHeight = 144,
    });
    assert(initialModel.has_value());
    assert(initialModel->displayEnabled);
    assert(initialModel->width == 160);
    assert(initialModel->height == 144);
    std::unordered_set<std::uint32_t> initialColors(
        initialModel->argbPixels.begin(),
        initialModel->argbPixels.end());
    assert(initialColors.size() > 1u);
    const auto initialSpritePixel =
        initialModel->argbPixels[24u * 160u + 24u];
    const auto backgroundPixel =
        initialModel->argbPixels[24u * 160u + 8u];
    assert(initialSpritePixel != backgroundPixel);

    GameGearMemoryMap memory;
    const std::vector<uint8_t> mappedRom(0x8000, 0xAAu);
    memory.mapRom(mappedRom.data(), mappedRom.size());
    assert(memory.read(0x7F00) == 0xAAu);

    Z80Interpreter cpu;
    assert(cpu.AF == 0u);
    assert(cpu.BC == 0u);
    assert(cpu.DE == 0u);
    assert(cpu.HL == 0u);
    assert(cpu.IX == 0u);
    assert(cpu.IY == 0u);
    assert(cpu.SP == 0u);
    assert(cpu.PC == 0u);
    assert(cpu.AF_ == 0u);
    assert(cpu.BC_ == 0u);
    assert(cpu.DE_ == 0u);
    assert(cpu.HL_ == 0u);
    assert(cpu.I == 0u);
    assert(cpu.R == 0u);
    assert(!cpu.IFF1);
    assert(!cpu.IFF2);
    assert(!cpu.IME);

    bool threwMissingMemoryInterface = false;
    try {
        (void)cpu.step();
    } catch (const std::runtime_error& ex) {
        threwMissingMemoryInterface = std::string_view(ex.what()) == "Z80 memory interface not set";
    }
    assert(threwMissingMemoryInterface);

    std::vector<uint8_t> z80Memory(0x10000u, 0x00u);
    cpu.setMemoryInterface(
        [&](uint16_t address) { return z80Memory[address]; },
        [&](uint16_t address, uint8_t value) { z80Memory[address] = value; }
    );
    cpu.reset();
    z80Memory[0x0000u] = 0x06u; // LD B,3
    z80Memory[0x0001u] = 0x03u;
    z80Memory[0x0002u] = 0x3Cu; // INC A
    z80Memory[0x0003u] = 0x10u; // DJNZ -3 (to INC A)
    z80Memory[0x0004u] = 0xFDu;
    z80Memory[0x0005u] = 0x18u; // JR -2
    z80Memory[0x0006u] = 0xFEu;
    assert(cpu.step() == 7u);
    assert(cpu.step() == 4u);
    assert(cpu.step() == 13u);
    assert(cpu.step() == 4u);
    assert(cpu.step() == 13u);
    assert(static_cast<uint8_t>((cpu.AF >> 8) & 0x00FFu) == 0x03u);
    assert(cpu.PC == 0x0002u);

    const auto logicalMask = static_cast<BMMQ::InputButtonMask>(
        BMMQ::inputButtonMask(BMMQ::InputButton::Right) |
        BMMQ::inputButtonMask(BMMQ::InputButton::Button1) |
        BMMQ::inputButtonMask(BMMQ::InputButton::Meta1));
    FixedInputPlugin inputPlugin(logicalMask);
    assert(gg.inputService().attachExternalAdapter(inputPlugin));
    assert(gg.inputService().resume());
    gg.serviceInput();
    assert(gg.currentDigitalInputMask().has_value());
    assert(*gg.currentDigitalInputMask() == logicalMask);
    assert((gg.runtimeContext().read8(0x00DCu) & 0x08u) == 0u);
    assert((gg.runtimeContext().read8(0x00DCu) & 0x10u) == 0u);
    assert((gg.runtimeContext().read8(0x00DCu) & 0x80u) == 0u);
    assert(stepUntil(gg, 128, [&] {
        return gg.runtimeContext().read8(0xC000u) != 0x00u;
    }));
    assert((gg.runtimeContext().read8(0xC000u) & 0x08u) == 0u);
    assert((gg.runtimeContext().read8(0xC000u) & 0x10u) == 0u);
    assert((gg.runtimeContext().read8(0xC000u) & 0x80u) == 0u);
    assert(stepUntil(gg, 256, [&] {
        return gg.runtimeContext().read8(0xFE01u) == 88u;
    }));
    auto pressedModel = gg.videoDebugFrameModel(BMMQ::VideoDebugRenderRequest{
        .frameWidth = 160,
        .frameHeight = 144,
    });
    assert(pressedModel.has_value());
    assert(pressedModel->argbPixels != initialModel->argbPixels);
    const auto movedSpritePixel =
        pressedModel->argbPixels[24u * 160u + 40u];
    assert(movedSpritePixel == initialSpritePixel);

    BMMQ::GameGearMachine scanlineMachine;
    scanlineMachine.loadRom(makeDisplayOnlyRom());
    auto scanlinePlugin = std::make_unique<FirstScanlinePlugin>();
    auto* firstScanline = scanlinePlugin.get();
    scanlineMachine.pluginManager().add(std::move(scanlinePlugin));
    scanlineMachine.pluginManager().initialize(scanlineMachine.mutableView());
    for (int i = 0; i < 1000 && !firstScanline->firstScanlineEvent.has_value(); ++i) {
        scanlineMachine.step();
    }
    assert(firstScanline->firstScanlineEvent.has_value());
    assert(firstScanline->firstScanlineEvent->value == 0u);
    assert(firstScanline->firstScanlineEvent->tick >= 300u);

    puts("Game Gear core basic smoke test passed.");
    return 0;
}
