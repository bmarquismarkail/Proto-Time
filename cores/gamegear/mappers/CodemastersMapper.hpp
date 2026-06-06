#pragma once
#include "../GameGearCartridge.hpp"
#include <vector>

// Codemasters-style mapper
// - Control registers are written to the low addresses of each 16KB slot
//   (writes anywhere in 0x0000-0x3FFF affect slot 0, 0x4000-0x7FFF slot 1,
//    and 0x8000-0xBFFF slot 2).
// - Initial bank mapping: slot0=0, slot1=1, slot2=0
// - Some titles (eg. Ernie Els Golf) provide on-cart 8KB RAM mapped into
//   0xA000-0xBFFF when a high-bit ($80) is written to the slot1 control value.
class CodemastersMapper : public GameGearCartridge {
public:
    CodemastersMapper() = default;
    ~CodemastersMapper() override = default;

    bool load(const uint8_t* data, size_t size) override {
        return GameGearCartridge::load(data, size);
    }

    void reset() override {
        GameGearCartridge::reset();
        // Codemasters initial mapping: slot0=0, slot1=1, slot2=0
        GameGearCartridge::write(0xFFFDu, 0u);
        GameGearCartridge::write(0xFFFEu, 1u);
        GameGearCartridge::write(0xFFFFu, 0u);
        extraRamMapped_ = false;
        extraRam_.clear();
        extraRamDirty_ = false;
    }

    bool handlesControlWrite(uint16_t addr) const noexcept override {
        // The control registers are mapped across the 3x16KB ROM slots.
        return addr < 0xC000u;
    }

    bool handlesMappedWrite(uint16_t addr) const noexcept override {
        // If the on-cart RAM is mapped, advertise that we accept mapped writes
        // for 0xA000-0xBFFF. Otherwise, fall back to base cartridge logic.
        if (extraRamMapped_ && addr >= 0xA000u && addr < 0xC000u) return true;
        return GameGearCartridge::handlesMappedWrite(addr);
    }

    uint8_t read(uint16_t addr) const override {
        if (extraRamMapped_ && addr >= 0xA000u && addr < 0xC000u) {
            const auto idx = static_cast<size_t>(addr - 0xA000u);
            return idx < extraRam_.size() ? extraRam_[idx] : 0xFFu;
        }
        return GameGearCartridge::read(addr);
    }

    void write(uint16_t addr, uint8_t value) override {
        if (addr < 0xC000u) {
            // Treat writes anywhere in a 16KB slot as a control write for that slot
            const std::size_t slot = static_cast<std::size_t>(addr / 0x4000u);
            const uint8_t bankVal = static_cast<uint8_t>(value & 0x7Fu);
            GameGearCartridge::write(static_cast<uint16_t>(0xFFFDu + slot), bankVal);

            // Special-case: Ernie Els style on-cart RAM mapping via high-bit on slot1 writes
            if (slot == 1u) {
                if ((value & 0x80u) != 0u) {
                    if (extraRam_.empty()) extraRam_.assign(0x2000u, 0u);
                    extraRamMapped_ = true;
                } else {
                    extraRamMapped_ = false;
                }
            }
            return;
        }

        if (extraRamMapped_ && addr >= 0xA000u && addr < 0xC000u) {
            const auto idx = static_cast<size_t>(addr - 0xA000u);
            if (idx < extraRam_.size()) {
                extraRam_[idx] = value;
                extraRamDirty_ = true;
            }
            return;
        }

        GameGearCartridge::write(addr, value);
    }

    [[nodiscard]] bool supportsSaveData() const noexcept override {
        return !extraRam_.empty() || GameGearCartridge::supportsSaveData();
    }

    [[nodiscard]] bool hasDirtySaveData() const noexcept override {
        return extraRamDirty_ || GameGearCartridge::hasDirtySaveData();
    }

    void markSaveClean() noexcept override {
        extraRamDirty_ = false;
        GameGearCartridge::markSaveClean();
    }

    std::vector<uint8_t> exportSaveData() const override {
        if (!extraRam_.empty()) return extraRam_;
        return GameGearCartridge::exportSaveData();
    }

    void importSaveData(const std::vector<uint8_t>& saveData) override {
        if (saveData.size() == 0x2000u) {
            extraRam_ = saveData;
            extraRamDirty_ = false;
        } else {
            GameGearCartridge::importSaveData(saveData);
        }
    }

private:
    std::vector<uint8_t> extraRam_;
    bool extraRamMapped_ = false;
    bool extraRamDirty_ = false;
};
