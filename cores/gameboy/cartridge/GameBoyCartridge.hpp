#ifndef GB_GAMEBOY_CARTRIDGE_HPP
#define GB_GAMEBOY_CARTRIDGE_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace GB {

enum class CartridgeMapper {
    None,
    MBC1,
    MBC2,
    MBC3,
    MBC5,
};

struct CartridgeMetadata {
    CartridgeMapper mapper = CartridgeMapper::None;
    uint8_t cartridgeType = 0x00u;
    uint8_t romSizeCode = 0x00u;
    uint8_t ramSizeCode = 0x00u;
    std::size_t romBankCount = 0;
    std::size_t externalRamSize = 0;
    bool hasBattery = false;
    bool hasRtc = false;
};

struct RtcSaveData {
    std::array<uint8_t, 5> registers{};
    bool latched = false;
};

struct CartridgeSaveData {
    std::vector<uint8_t> externalRam;
    std::optional<RtcSaveData> rtc;
};

struct CartridgeWriteResult {
    bool handled = false;
    bool romBankChanged = false;
};

[[nodiscard]] inline std::size_t ramSizeForHeader(uint8_t ramSizeCode, CartridgeMapper mapper)
{
    if (mapper == CartridgeMapper::MBC2) {
        return 0x0200u;
    }

    switch (ramSizeCode) {
    case 0x00:
        return 0;
    case 0x01:
        return 0x0800u;
    case 0x02:
        return 0x2000u;
    case 0x03:
        return 0x8000u;
    case 0x04:
        return 0x20000u;
    case 0x05:
        return 0x10000u;
    default:
        return 0;
    }
}

[[nodiscard]] inline CartridgeMapper mapperForType(uint8_t type)
{
    switch (type) {
    case 0x00:
        return CartridgeMapper::None;
    case 0x01:
    case 0x02:
    case 0x03:
        return CartridgeMapper::MBC1;
    case 0x05:
    case 0x06:
        return CartridgeMapper::MBC2;
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        return CartridgeMapper::MBC3;
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
        return CartridgeMapper::MBC5;
    default:
        return CartridgeMapper::None;
    }
}

[[nodiscard]] inline bool typeHasBattery(uint8_t type)
{
    switch (type) {
    case 0x03:
    case 0x06:
    case 0x09:
    case 0x0F:
    case 0x10:
    case 0x13:
    case 0x1B:
    case 0x1E:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool typeHasRtc(uint8_t type)
{
    return type == 0x0Fu || type == 0x10u;
}

[[nodiscard]] inline CartridgeMetadata parseCartridgeMetadata(std::span<const uint8_t> bytes)
{
    CartridgeMetadata metadata{};
    metadata.cartridgeType = bytes.size() > 0x147u ? bytes[0x147u] : 0x00u;
    metadata.romSizeCode = bytes.size() > 0x148u ? bytes[0x148u] : 0x00u;
    metadata.ramSizeCode = bytes.size() > 0x149u ? bytes[0x149u] : 0x00u;
    metadata.mapper = mapperForType(metadata.cartridgeType);
    metadata.romBankCount = (bytes.size() + 0x3FFFu) / 0x4000u;
    metadata.externalRamSize = ramSizeForHeader(metadata.ramSizeCode, metadata.mapper);
    metadata.hasBattery = typeHasBattery(metadata.cartridgeType);
    metadata.hasRtc = typeHasRtc(metadata.cartridgeType);
    return metadata;
}

[[nodiscard]] inline CartridgeMetadata parseCartridgeMetadata(const std::vector<uint8_t>& bytes)
{
    return parseCartridgeMetadata(std::span<const uint8_t>(bytes.data(), bytes.size()));
}

class GameBoyCartridge {
public:
    void load(const std::vector<uint8_t>& bytes)
    {
        if (bytes.empty()) {
            throw std::invalid_argument("ROM must not be empty");
        }

        metadata_ = parseCartridgeMetadata(bytes);
        if (metadata_.mapper == CartridgeMapper::None && bytes.size() > 0x8000u) {
            throw std::invalid_argument("ROM exceeds first-milestone Game Boy ROM window");
        }

        rom_ = bytes;
        if (metadata_.romBankCount == 0) {
            metadata_.romBankCount = 1;
        }
        currentRomBank_ = metadata_.romBankCount > 1 ? 1 : 0;
        currentRamBank_ = 0;
        ramEnabled_ = false;
        selectedRtcRegister_ = 0xFFu;
        rtcLatched_ = false;
        rtcRegisters_.fill(0x00u);
        externalRam_.assign(metadata_.externalRamSize, 0x00u);
        mbc1LowBankBits_ = 1;
        mbc1UpperBankBits_ = 0;
        mbc1BankingModeSelect_ = false;
        saveDirty_ = false;
    }

    [[nodiscard]] const CartridgeMetadata& metadata() const noexcept
    {
        return metadata_;
    }

    [[nodiscard]] bool supportsBatterySave() const noexcept
    {
        return metadata_.hasBattery;
    }

    [[nodiscard]] bool supportsRtc() const noexcept
    {
        return metadata_.hasRtc;
    }

    [[nodiscard]] bool hasDirtySaveData() const noexcept
    {
        return saveDirty_;
    }

    void markSaveClean() noexcept
    {
        saveDirty_ = false;
    }

    [[nodiscard]] CartridgeSaveData exportSaveData() const
    {
        CartridgeSaveData save{};
        save.externalRam = externalRam_;
        if (supportsRtc()) {
            save.rtc = RtcSaveData{rtcRegisters_, rtcLatched_};
        }
        return save;
    }

    void importSaveData(const CartridgeSaveData& save)
    {
        if (!externalRam_.empty()) {
            const auto count = std::min(externalRam_.size(), save.externalRam.size());
            std::copy_n(save.externalRam.begin(), count, externalRam_.begin());
            if (count < externalRam_.size()) {
                std::fill(externalRam_.begin() + static_cast<std::ptrdiff_t>(count), externalRam_.end(), 0x00u);
            }
        }
        if (supportsRtc() && save.rtc.has_value()) {
            rtcRegisters_ = save.rtc->registers;
            rtcLatched_ = save.rtc->latched;
        }
        markSaveClean();
    }

    [[nodiscard]] bool read(uint16_t address, std::span<uint8_t> value) const
    {
        if (address < 0xA000u || address >= 0xC000u || metadata_.mapper == CartridgeMapper::None) {
            return false;
        }

        if (!ramEnabled_) {
            std::fill(value.begin(), value.end(), static_cast<uint8_t>(0xFFu));
            return true;
        }

        if (rtcRegisterSelected()) {
            const auto rtcIndex = static_cast<std::size_t>(selectedRtcRegister_ - 0x08u);
            std::fill(value.begin(), value.end(), rtcRegisters_[rtcIndex]);
            return true;
        }

        if (externalRam_.empty()) {
            std::fill(value.begin(), value.end(), static_cast<uint8_t>(0xFFu));
            return true;
        }

        const auto bankSize = ramBankSize();
        const auto bankIndex = selectedRamBankIndex();
        const auto baseOffset = static_cast<std::size_t>(address - 0xA000u);
        for (std::size_t i = 0; i < value.size(); ++i) {
            std::size_t localOffset = 0;
            if (metadata_.mapper == CartridgeMapper::MBC2) {
                localOffset = (baseOffset + i) & 0x01FFu;
            } else {
                localOffset = (baseOffset + i) % bankSize;
            }
            const auto ramIndex = bankIndex * bankSize + localOffset;
            if (ramIndex >= externalRam_.size()) {
                value[i] = 0xFFu;
            } else if (metadata_.mapper == CartridgeMapper::MBC2) {
                value[i] = static_cast<uint8_t>(0xF0u | (externalRam_[ramIndex] & 0x0Fu));
            } else {
                value[i] = externalRam_[ramIndex];
            }
        }
        return true;
    }

    [[nodiscard]] uint8_t read(uint16_t address) const
    {
        uint8_t value = 0xFFu;
        (void)read(address, std::span<uint8_t>(&value, 1));
        return value;
    }

    CartridgeWriteResult write(uint16_t address, uint8_t data)
    {
        if (address >= 0xA000u && address < 0xC000u) {
            return {writeExternalArea(address, std::span<const uint8_t>(&data, 1)), false};
        }
        if (address < 0x8000u) {
            return writeControl(address, data);
        }
        return {};
    }

    [[nodiscard]] bool write(uint16_t address, std::span<const uint8_t> value)
    {
        if (address >= 0xA000u && address < 0xC000u) {
            return writeExternalArea(address, value);
        }
        if (value.size() == 1 && address < 0x8000u) {
            return writeControl(address, value[0]).handled;
        }
        return false;
    }

    [[nodiscard]] std::size_t fixedBankIndex() const
    {
        if (metadata_.mapper == CartridgeMapper::MBC1 && mbc1BankingModeSelect_) {
            return ((static_cast<std::size_t>(mbc1UpperBankBits_) << 5) % metadata_.romBankCount);
        }
        return 0;
    }

    [[nodiscard]] std::size_t switchableBankIndex() const
    {
        if (metadata_.romBankCount <= 1) {
            return 0;
        }

        switch (metadata_.mapper) {
        case CartridgeMapper::None:
            return metadata_.romBankCount > 1 ? 1 : 0;
        case CartridgeMapper::MBC1: {
            std::size_t bank = (static_cast<std::size_t>(mbc1UpperBankBits_) << 5) |
                               static_cast<std::size_t>(mbc1LowBankBits_ & 0x1Fu);
            bank &= 0x7Fu;
            bank %= metadata_.romBankCount;
            if ((bank & 0x1Fu) == 0) {
                bank = (bank + 1) % metadata_.romBankCount;
            }
            return bank == 0 ? 1 % metadata_.romBankCount : bank;
        }
        case CartridgeMapper::MBC2:
        case CartridgeMapper::MBC3:
            return currentRomBank_ % metadata_.romBankCount == 0
                ? 1 % metadata_.romBankCount
                : currentRomBank_ % metadata_.romBankCount;
        case CartridgeMapper::MBC5:
            return currentRomBank_ % metadata_.romBankCount;
        default:
            return 0;
        }
    }

    void copyRomBankWindow(std::size_t bankIndex, std::span<uint8_t> window) const
    {
        std::fill(window.begin(), window.end(), static_cast<uint8_t>(0xFFu));
        const auto offset = bankIndex * 0x4000u;
        if (offset < rom_.size()) {
            const auto count = std::min<std::size_t>(window.size(), rom_.size() - offset);
            std::copy_n(rom_.begin() + static_cast<std::ptrdiff_t>(offset), count, window.begin());
        }
    }

private:
    [[nodiscard]] std::size_t ramBankSize() const noexcept
    {
        return metadata_.mapper == CartridgeMapper::MBC2 ? 0x0200u : 0x2000u;
    }

    [[nodiscard]] std::size_t selectedRamBankIndex() const
    {
        const auto bankSize = ramBankSize();
        if (bankSize == 0 || externalRam_.empty()) {
            return 0;
        }
        const auto bankCount = std::max<std::size_t>(std::size_t{1}, externalRam_.size() / bankSize);

        switch (metadata_.mapper) {
        case CartridgeMapper::MBC1:
            return mbc1BankingModeSelect_ ? (mbc1UpperBankBits_ % bankCount) : 0;
        case CartridgeMapper::MBC3:
        case CartridgeMapper::MBC5:
            return currentRamBank_ % bankCount;
        case CartridgeMapper::MBC2:
        case CartridgeMapper::None:
            return 0;
        }

        return 0;
    }

    [[nodiscard]] bool rtcRegisterSelected() const noexcept
    {
        return supportsRtc() && selectedRtcRegister_ >= 0x08u && selectedRtcRegister_ <= 0x0Cu;
    }

    void markDirtyIfPersistent() noexcept
    {
        if (supportsBatterySave()) {
            saveDirty_ = true;
        }
    }

    [[nodiscard]] bool writeExternalArea(uint16_t address, std::span<const uint8_t> value)
    {
        if (address < 0xA000u || address >= 0xC000u || metadata_.mapper == CartridgeMapper::None) {
            return false;
        }

        if (!ramEnabled_) {
            return true;
        }

        if (rtcRegisterSelected()) {
            if (value.empty()) {
                return false;
            }
            const auto rtcIndex = static_cast<std::size_t>(selectedRtcRegister_ - 0x08u);
            if (rtcRegisters_[rtcIndex] != value.front()) {
                rtcRegisters_[rtcIndex] = value.front();
                markDirtyIfPersistent();
            }
            return true;
        }

        if (externalRam_.empty()) {
            return true;
        }

        const auto bankSize = ramBankSize();
        const auto bankIndex = selectedRamBankIndex();
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto localOffset = metadata_.mapper == CartridgeMapper::MBC2
                ? ((static_cast<std::size_t>(address - 0xA000u) + i) & 0x01FFu)
                : (static_cast<std::size_t>(address - 0xA000u) + i);
            const auto ramIndex = bankIndex * bankSize + localOffset;
            if (ramIndex >= externalRam_.size()) {
                continue;
            }
            const auto storedValue = metadata_.mapper == CartridgeMapper::MBC2
                ? static_cast<uint8_t>(value[i] & 0x0Fu)
                : value[i];
            if (externalRam_[ramIndex] != storedValue) {
                externalRam_[ramIndex] = storedValue;
                markDirtyIfPersistent();
            }
        }
        return true;
    }

    [[nodiscard]] CartridgeWriteResult writeControl(uint16_t address, uint8_t data)
    {
        if (address >= 0x8000u || metadata_.mapper == CartridgeMapper::None) {
            return {};
        }

        switch (metadata_.mapper) {
        case CartridgeMapper::MBC1:
            if (address < 0x2000u) {
                ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                return {true, false};
            }
            if (address < 0x4000u) {
                mbc1LowBankBits_ = data & 0x1Fu;
                if (mbc1LowBankBits_ == 0) {
                    mbc1LowBankBits_ = 1;
                }
                return {true, true};
            }
            if (address < 0x6000u) {
                mbc1UpperBankBits_ = data & 0x03u;
                return {true, true};
            }
            mbc1BankingModeSelect_ = (data & 0x01u) != 0;
            return {true, true};
        case CartridgeMapper::MBC2:
            if (address < 0x2000u) {
                if ((address & 0x0100u) == 0u) {
                    ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                }
                return {true, false};
            }
            if (address < 0x4000u) {
                if ((address & 0x0100u) != 0u) {
                    currentRomBank_ = data & 0x0Fu;
                    if (currentRomBank_ == 0) {
                        currentRomBank_ = 1;
                    }
                    return {true, true};
                }
                return {true, false};
            }
            return {true, false};
        case CartridgeMapper::MBC3:
            if (address < 0x2000u) {
                ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                return {true, false};
            }
            if (address < 0x4000u) {
                currentRomBank_ = data & 0x7Fu;
                if (currentRomBank_ == 0) {
                    currentRomBank_ = 1;
                }
                return {true, true};
            }
            if (address < 0x6000u) {
                if (data <= 0x03u) {
                    currentRamBank_ = data & 0x03u;
                    selectedRtcRegister_ = 0xFFu;
                } else if (supportsRtc() && data >= 0x08u && data <= 0x0Cu) {
                    selectedRtcRegister_ = data;
                }
                return {true, false};
            }
            if (rtcLatched_ != ((data & 0x01u) != 0u)) {
                rtcLatched_ = (data & 0x01u) != 0u;
                if (supportsRtc()) {
                    markDirtyIfPersistent();
                }
            }
            return {true, false};
        case CartridgeMapper::MBC5:
            if (address < 0x2000u) {
                ramEnabled_ = (data & 0x0Fu) == 0x0Au;
                return {true, false};
            }
            if (address < 0x3000u) {
                currentRomBank_ = (currentRomBank_ & 0x100u) | data;
                return {true, true};
            }
            if (address < 0x4000u) {
                currentRomBank_ = ((static_cast<std::size_t>(data & 0x01u) << 8) | (currentRomBank_ & 0xFFu));
                return {true, true};
            }
            if (address < 0x6000u) {
                currentRamBank_ = data & 0x0Fu;
                return {true, false};
            }
            return {true, false};
        case CartridgeMapper::None:
            return {};
        }

        return {};
    }

    CartridgeMetadata metadata_{};
    std::vector<uint8_t> rom_;
    std::vector<uint8_t> externalRam_;
    std::size_t currentRomBank_ = 1;
    std::size_t currentRamBank_ = 0;
    bool ramEnabled_ = false;
    uint8_t selectedRtcRegister_ = 0xFFu;
    bool rtcLatched_ = false;
    std::array<uint8_t, 5> rtcRegisters_{};
    uint8_t mbc1LowBankBits_ = 1;
    uint8_t mbc1UpperBankBits_ = 0;
    bool mbc1BankingModeSelect_ = false;
    bool saveDirty_ = false;
};

} // namespace GB

#endif // GB_GAMEBOY_CARTRIDGE_HPP
