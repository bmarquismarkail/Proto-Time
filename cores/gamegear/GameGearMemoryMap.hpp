#pragma once
// Sega Game Gear memory map stub
// References: SMS Power, Charles MacDonald

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

class GameGearInput;
class GameGearMapper;
class GameGearPSG;
class GameGearVDP;
class GameGearMemoryMap {
public:
    GameGearMemoryMap();
    ~GameGearMemoryMap();

    void reset();
    uint8_t read(uint16_t addr) const;
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

    // Debug / introspection
    [[nodiscard]] uint8_t ioControlValue() const noexcept;
    [[nodiscard]] uint8_t memoryControlValue() const noexcept;

private:
    GameGearInput* input = nullptr;
    GameGearMapper* cartridge = nullptr;
    GameGearPSG* psg = nullptr;
    GameGearVDP* vdp = nullptr;
    uint8_t memoryControl_ = 0xFFu;
    uint8_t ioControl_ = 0xFFu;
    std::array<uint8_t, 0x2000> ram{}; // 8KB RAM
    std::vector<uint8_t> bios_{};
};
