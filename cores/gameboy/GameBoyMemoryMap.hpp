#pragma once
// Game Boy memory map abstraction.
// References: Pan Docs (https://gbdev.io/pandocs/)
//
// Responsibilities:
//   - VRAM (0x8000-0x9FFF), WRAM (0xC000-0xDFFF)
//   - Echo RAM (0xE000-0xFDFF) mirror of WRAM
//   - External RAM (0xA000-0xBFFF) via mapper callback
//   - OAM (0xFE00-0xFE9F), I/O (0xFF00-0xFF7F), HRAM (0xFF80-0xFFFE)
//   - Boot ROM overlay (0x0000-0x00FF) before FF50 unlock
//   - ROM/RAM window installation for bank switching

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "../../memory/MemoryStorage.hpp"

namespace GB {

class GameBoyMapper;
class GameBoyCartridge;

class GameBoyMemoryMap final : public BMMQ::MemoryStorage<uint16_t, uint8_t> {
public:
    GameBoyMemoryMap();
    ~GameBoyMemoryMap() = default;

    void reset();

    // Read/write through the full memory map
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);
    void read(std::span<uint8_t> stream, uint16_t address) const override;
    void write(std::span<const uint8_t> value, uint16_t address) override;

    // Boot ROM overlay at $0000-$00FF
    void mapBootRom(const uint8_t* data, std::size_t size);
    void clearBootRom();
    [[nodiscard]] bool hasBootRom() const noexcept { return !bootRom_.empty(); }
    [[nodiscard]] bool bootRomMapped() const noexcept { return bootRomActive_; }

    // Mapper callback for external RAM reads/writes
    void setMapper(GameBoyMapper* mapper) noexcept { mapper_ = mapper; }
    void setCartridge(GameBoyCartridge* cartridge) noexcept { cartridge_ = cartridge; }
    void setWriteObserver(std::function<void(uint16_t, uint8_t)> observer)
    {
        writeObserver_ = std::move(observer);
    }
    void setIoRegisterRaw(uint16_t address, uint8_t value);

    // ROM/RAM window installation (called by mapper on bank change)
    void installRomWindow(uint16_t base, std::span<const uint8_t> data);
    void clearRomWindow(uint16_t base);

    // Debug introspection
    [[nodiscard]] std::span<const uint8_t> vramSpan() const noexcept { return vram_; }
    [[nodiscard]] std::span<uint8_t> wramSpan() noexcept { return wram_; }
    [[nodiscard]] std::span<const uint8_t> oamSpan() const noexcept { return oam_; }

    // Expose raw memory storage for CPU attach (flat 64KB view)
    [[nodiscard]] std::span<uint8_t> storageSpan() noexcept { return storage_; }
    [[nodiscard]] std::span<const uint8_t> storageSpan() const noexcept { return storage_; }

private:
    // Resolve echo RAM address (E000-FDFF -> C000-DDFF)
    [[nodiscard]] static uint16_t resolveEchoAddress(uint16_t address) noexcept;

    // Internal read/write without echo resolution
    [[nodiscard]] uint8_t readRaw(uint16_t addr) const;
    void writeRaw(uint16_t addr, uint8_t value);

    // Boot ROM read interceptor
    [[nodiscard]] bool handleBootRomRead(uint16_t addr, std::span<uint8_t> out) const;
    // Special write interceptor (cartridge RAM, FF50 boot ROM disable, I/O)
    bool handleSpecialWrite(uint16_t addr, std::span<const uint8_t> value);

    // Mapper callback for external RAM
    bool handleCartridgeRamRead(uint16_t addr, std::span<uint8_t> out) const;
    bool handleCartridgeRamWrite(uint16_t addr, std::span<const uint8_t> value);

    // ROM window storage (4KB windows at 0x0000 or 0x4000)
    struct RomWindow {
        std::vector<uint8_t> data;
        bool active = false;
    };

    // Memory regions
    std::array<uint8_t, 0x2000> vram_{};       // 8KB VRAM
    std::array<uint8_t, 0x2000> wram_{};       // 8KB WRAM (C000-DFFF)
    std::array<uint8_t, 0xA0> oam_{};          // 160 bytes OAM (FE00-FE9F)
    std::array<uint8_t, 0x80> ioRegs_{};       // 128 bytes I/O (FF00-FF7F)
    std::array<uint8_t, 0x80> hram_{};         // 128 bytes HRAM/IE storage
    std::array<uint8_t, 0x10000> storage_{};   // Flat 64KB storage for CPU attach

    // Boot ROM
    std::vector<uint8_t> bootRom_{};
    bool bootRomActive_ = false;
    mutable bool dmaActive_ = false;

    // ROM windows (banked ROM visible at 0x0000 or 0x4000)
    RomWindow romWindow0x0000_{};
    RomWindow romWindow0x4000_{};

    // Mapper reference for external RAM
    GameBoyMapper* mapper_ = nullptr;
    GameBoyCartridge* cartridge_ = nullptr;
    std::function<void(uint16_t, uint8_t)> writeObserver_{};
};

} // namespace GB
