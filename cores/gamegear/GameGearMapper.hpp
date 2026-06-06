#pragma once
// Abstract Game Gear / SMS cartridge mapper interface

#include <cstdint>
#include <cstddef>
#include <vector>

class GameGearMapper {
public:
    GameGearMapper() = default;
    virtual ~GameGearMapper() = default;

    // Load ROM image (nullptr,size==0 clears)
    virtual bool load(const uint8_t* data, size_t size) = 0;
    virtual void reset() = 0;
    virtual bool loaded() const noexcept = 0;

    // Cartridge I/O hooks
    virtual bool handlesControlWrite(uint16_t addr) const noexcept = 0;
    virtual bool handlesMappedWrite(uint16_t addr) const noexcept = 0;
    virtual uint8_t read(uint16_t addr) const = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;

    // Save (SRAM) support
    virtual bool supportsSaveData() const noexcept = 0;
    virtual bool hasDirtySaveData() const noexcept = 0;
    virtual void markSaveClean() noexcept = 0;
    virtual std::vector<uint8_t> exportSaveData() const = 0;
    virtual void importSaveData(const std::vector<uint8_t>& saveData) = 0;
};
