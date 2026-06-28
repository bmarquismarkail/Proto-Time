#pragma once
// Game Boy MBC/cartridge abstraction.
// References: Pan Docs (MBC1, MBC2, MBC3, MBC5)
//
// Responsibilities:
//   - ROM bank switching (0x4000-0x7FFF)
//   - RAM banking (0xA000-0xBFFF)
//   - MBC type detection from cartridge header
//   - External RAM read/write with dirty tracking

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "cartridge/GameBoyCartridge.hpp"

namespace GB {

class GameBoyMapper {
public:
    GameBoyMapper();
    ~GameBoyMapper() = default;

    void load(const std::vector<uint8_t>& romData);
    void reset();

    // Write handler: called from memory map on $4000-$BFFF writes
    struct WriteResult {
        bool handled = false;
        bool romBankChanged = false;
    };
    WriteResult write(uint16_t address, uint8_t value);

    // Read external RAM (called by memory map)
    [[nodiscard]] bool ramRead(uint16_t addr, std::span<uint8_t> out) const;
    // Write external RAM (called by memory map)
    bool ramWrite(uint16_t addr, std::span<const uint8_t> value);

    // Install ROM window into memory map (bank change notification)
    void copyRomBankWindow(std::size_t bankIndex, std::span<uint8_t> window) const;
    [[nodiscard]] std::size_t currentRomBank() const noexcept { return effectiveRomBank_; }

    // Metadata accessors
    [[nodiscard]] CartridgeMapper mapperType() const noexcept { return mapperType_; }
    [[nodiscard]] bool supportsBatterySave() const noexcept { return hasBattery_; }
    [[nodiscard]] bool hasDirtySaveData() const noexcept { return !externalRam_.empty() && dirty_; }
    [[nodiscard]] std::size_t externalRamSize() const noexcept { return externalRamSize_; }
    [[nodiscard]] std::size_t romBankCount() const noexcept { return romBankCount_; }

    // Save state serialization — deterministic, self-delimiting, CRC trailer
    struct SaveState {
        // ROM identity (not full ROM data — ROM is assumed loaded by Machine)
        std::size_t romSize;
        std::size_t romBankCount;
        // Banking state — must export/import for exact state restoration
        uint16_t romBankLow;
        std::size_t effectiveRomBank;
        uint8_t ramBankSelect;
        uint8_t ramBankMode;
        bool ramEnabled;
        // Dirty persistence
        bool dirty;
        bool hasBattery;
        // External RAM data (only when dirty)
        bool externalRamValid;
        std::vector<uint8_t> externalRam;
    };

    [[nodiscard]] SaveState exportState() const;
    void importState(const SaveState& state);

    // Save data export/import (legacy — kept for backward compat)
    struct [[deprecated("Use exportState()/importState() instead")]] SaveSnapshot {
        std::vector<uint8_t> externalRam;
    };
    [[deprecated("Use exportState() instead")]]
    [[nodiscard]] SaveSnapshot extractDirtySaveSnapshot() const;
    [[deprecated("Use importState() instead")]]
    static void flushSnapshot(const SaveSnapshot& snapshot);

private:
    // MBC1 banking logic
    void updateMbc1Banking();
    void updateMbc5Banking();

    // Check if address is in RAM range
    [[nodiscard]] bool isRamAddress(uint16_t addr) const noexcept;

    CartridgeMapper mapperType_ = CartridgeMapper::None;
    std::vector<uint8_t> romData_{};
    std::size_t romSize_ = 0;
    std::size_t romBankCount_ = 0;
    std::size_t externalRamSize_ = 0;
    std::vector<uint8_t> externalRam_{};

    // Banking state
    uint16_t romBankLow_ = 1U;
    std::size_t effectiveRomBank_ = 1U;
    uint8_t ramBankSelect_ = 0u;
    uint8_t ramBankMode_ = 0u; // 0 = bank select, 1 = ROM bank bits 5-6
    bool ramEnabled_ = false;

    // Dirty flag for save
    bool dirty_ = false;
    bool hasBattery_ = false;
};

} // namespace GB
