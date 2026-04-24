#pragma once
// Sega Game Gear memory map stub
// References: SMS Power, Charles MacDonald

#include <cstdint>


#include <vector>
#include <array>

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

    void setCartridge(GameGearMapper* cartridgePtr);
    void setInput(GameGearInput* inputPtr);
    void setPsg(GameGearPSG* psgPtr);
    void setVdp(GameGearVDP* vdpPtr);
    [[nodiscard]] uint8_t readIoPort(uint8_t port);
    void writeIoPort(uint8_t port, uint8_t value);

private:
    GameGearInput* input = nullptr;
    GameGearMapper* cartridge = nullptr;
    GameGearPSG* psg = nullptr;
    GameGearVDP* vdp = nullptr;
    std::array<uint8_t, 0x2000> ram{}; // 8KB RAM
};
