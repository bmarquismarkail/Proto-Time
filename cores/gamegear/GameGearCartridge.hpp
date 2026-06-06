#pragma once
// Sega Game Gear cartridge implementation (mapper)
// References: SMS Power, Charles MacDonald

#include "GameGearMapper.hpp"

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

class GameGearCartridge : public GameGearMapper {
public:
    GameGearCartridge();
    ~GameGearCartridge() override;

    bool load(const uint8_t* data, size_t size);
    void reset();
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] bool handlesControlWrite(uint16_t addr) const noexcept;
    [[nodiscard]] bool handlesMappedWrite(uint16_t addr) const noexcept;
    [[nodiscard]] uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);
    [[nodiscard]] bool supportsSaveData() const noexcept;
    [[nodiscard]] bool hasDirtySaveData() const noexcept;
    void markSaveClean() noexcept;
    [[nodiscard]] std::vector<uint8_t> exportSaveData() const;
    void importSaveData(const std::vector<uint8_t>& saveData);

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
    bool saveDirty_ = false;

public:
    // Debug / introspection
    [[nodiscard]] std::array<uint8_t,3> bankRegisters() const noexcept { return bankRegisters_; }
    [[nodiscard]] uint8_t controlRegister() const noexcept { return controlRegister_; }
};
