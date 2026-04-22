#pragma once
// Sega Game Gear memory map stub
// References: SMS Power, Charles MacDonald

#include <cstdint>


#include <vector>
#include <array>

class GameGearInput;
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

    void setInput(GameGearInput* inputPtr);
    void setVdp(GameGearVDP* vdpPtr);
    [[nodiscard]] uint8_t readIoPort(uint8_t port) const;
    void writeIoPort(uint8_t port, uint8_t value);

private:
    GameGearInput* input = nullptr;
    GameGearVDP* vdp = nullptr;
    std::vector<uint8_t> rom;
    std::array<uint8_t, 0x2000> ram{}; // 8KB RAM
};
