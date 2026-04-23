#pragma once
// Sega Game Gear cartridge interface stub
// References: SMS Power, Charles MacDonald

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

class GameGearCartridge {
public:
    GameGearCartridge();
    ~GameGearCartridge();

    bool load(const uint8_t* data, size_t size);
    void reset();
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool handlesControlWrite(uint16_t addr) const noexcept;
    [[nodiscard]] bool handlesMappedWrite(uint16_t addr) const noexcept;
    [[nodiscard]] uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);

private:
    static constexpr std::size_t kPageSize = 0x4000u;
    static constexpr std::size_t kSramWindowSize = 0x4000u;
    static constexpr std::size_t kSramSize = kSramWindowSize * 2u;

    [[nodiscard]] std::size_t numBanks() const noexcept;
    [[nodiscard]] std::size_t pageBank(std::size_t pageIndex) const noexcept;
    [[nodiscard]] bool sramEnabled() const noexcept;
    [[nodiscard]] std::size_t sramOffset(uint16_t addr) const noexcept;

    std::vector<uint8_t> rom;
    std::vector<uint8_t> sram;
    std::array<uint8_t, 3> bankRegisters_{};
    uint8_t controlRegister_ = 0u;
};
