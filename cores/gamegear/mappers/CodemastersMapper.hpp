#pragma once
#include "../GameGearCartridge.hpp"
#include <stdexcept>
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

    std::vector<uint8_t> exportState() const override {
        auto state = GameGearCartridge::exportState();
        const auto appendU32 = [&state](std::uint32_t value) {
            state.push_back(static_cast<uint8_t>(value & 0xFFu));
            state.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
            state.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
            state.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
        };
        appendU32(static_cast<std::uint32_t>(extraRam_.size()));
        state.insert(state.end(), extraRam_.begin(), extraRam_.end());
        state.push_back(extraRamMapped_ ? 1u : 0u);
        state.push_back(extraRamDirty_ ? 1u : 0u);
        return state;
    }

    void importState(const std::vector<uint8_t>& state) override {
        const auto baseSize = GameGearCartridge::exportState().size();
        if (state.size() < baseSize + 6u) {
            throw std::invalid_argument("Codemasters mapper state truncated");
        }
        GameGearCartridge::importState(std::vector<uint8_t>(state.begin(), state.begin() + static_cast<std::ptrdiff_t>(baseSize)));
        std::size_t pos = baseSize;
        const auto readU32 = [&state, &pos]() {
            if (state.size() - pos < 4u) {
                throw std::invalid_argument("Codemasters mapper state truncated");
            }
            const auto value = static_cast<std::uint32_t>(state[pos]) |
                (static_cast<std::uint32_t>(state[pos + 1u]) << 8u) |
                (static_cast<std::uint32_t>(state[pos + 2u]) << 16u) |
                (static_cast<std::uint32_t>(state[pos + 3u]) << 24u);
            pos += 4u;
            return value;
        };
        const auto extraSize = static_cast<std::size_t>(readU32());
        if (extraSize > 0x2000u || state.size() - pos != extraSize + 2u) {
            throw std::invalid_argument("Codemasters mapper state extra RAM invalid");
        }
        extraRam_.assign(state.begin() + static_cast<std::ptrdiff_t>(pos),
                         state.begin() + static_cast<std::ptrdiff_t>(pos + extraSize));
        pos += extraSize;
        extraRamMapped_ = state[pos++] != 0u;
        extraRamDirty_ = state[pos++] != 0u;
    }

private:
    std::vector<uint8_t> extraRam_;
    bool extraRamMapped_ = false;
    bool extraRamDirty_ = false;
};
