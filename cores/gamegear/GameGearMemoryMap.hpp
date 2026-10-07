#pragma once
#include "machine/plugins/debug/DebugEngine.hpp"
// Sega Game Gear memory map stub
// References: SMS Power, Charles MacDonald

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>
#include "space/Capture.hpp"

class GameGearInput;
class GameGearMapper;
class GameGearPSG;
class GameGearVDP;
class GameGearMemoryMap {
public:
    GameGearMemoryMap();
    ~GameGearMemoryMap();
    GameGearMemoryMap(const GameGearMemoryMap&) = default;
    GameGearMemoryMap& operator=(const GameGearMemoryMap&) = default;
    GameGearMemoryMap(GameGearMemoryMap&&) = default;
    GameGearMemoryMap& operator=(GameGearMemoryMap&&) = default;
    BMMQ::Debug::DebugEngine* debugEngine = nullptr;
    void debugCommitRam(uint16_t address, uint8_t value) noexcept { ram[address & 0x1fff] = value; }

    void reset();
    uint8_t read(uint16_t addr) const;
    [[nodiscard]] bool peekCodeByte(uint16_t addr, uint8_t& value) const noexcept;
    [[nodiscard]] uint64_t analysisLocation(uint16_t addr,bool write=false) const noexcept;
    [[nodiscard]] bool analysisRam(uint16_t addr) const noexcept;
    [[nodiscard]] std::size_t analysisRamCapacity() const noexcept;
    [[nodiscard]] bool analysisPhysicalRamByte(std::size_t, uint8_t&) const noexcept;
    BMMQ::Space::Capture* analysisCapture=nullptr;
    void write(uint16_t addr, uint8_t value);

    // ROM and RAM mapping
    void mapRom(const uint8_t* data, size_t size);
    void clearRom();
    // Optional BIOS (boot ROM) mapping for Game Gear: 1KB at $0000-$03FF
    void mapBios(const uint8_t* data, size_t size);
    void clearBios();
    [[nodiscard]] bool hasBios() const noexcept;

    void setCartridge(GameGearMapper* cartridgePtr);
    void setInput(GameGearInput* inputPtr);
    void setPsg(GameGearPSG* psgPtr);
    void setVdp(GameGearVDP* vdpPtr);
    [[nodiscard]] uint8_t readIoPort(uint8_t port);
    void writeIoPort(uint8_t port, uint8_t value);
    [[nodiscard]] uint8_t readEffectiveIoPort(uint16_t port);
    void writeEffectiveIoPort(uint16_t port,uint8_t value);

    // Debug / introspection
    [[nodiscard]] uint8_t ioControlValue() const noexcept;
    [[nodiscard]] uint8_t memoryControlValue() const noexcept;
    [[nodiscard]] uint64_t codeMappingGeneration() const noexcept;
    [[nodiscard]] std::vector<uint8_t> exportState() const;
    void importState(const std::vector<uint8_t>& state);

private:
    uint8_t readCanonical(uint16_t addr) const;
    void writeCanonical(uint16_t addr,uint8_t value);
    uint8_t readIoCanonical(uint8_t port);
    void writeIoCanonical(uint8_t port,uint8_t value);
    GameGearInput* input = nullptr;
    GameGearMapper* cartridge = nullptr;
    GameGearPSG* psg = nullptr;
    GameGearVDP* vdp = nullptr;
    uint8_t memoryControl_ = 0xFFu;
    uint8_t ioControl_ = 0xFFu;
    std::array<uint8_t, 0x2000> ram{}; // 8KB RAM
    std::vector<uint8_t> bios_{};
    uint64_t codeMappingGeneration_ = 1u;
};
