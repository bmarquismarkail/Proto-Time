#include "GameGearMemoryMap.hpp"

#include "GameGearCartridge.hpp"
#include "GameGearInput.hpp"
#include "GameGearPSG.hpp"
#include "GameGearVDP.hpp"

namespace {
GameGearCartridge& fallbackCartridgeStorage() {
    static GameGearCartridge cartridge;
    return cartridge;
}

constexpr uint8_t kMemoryControlBiosDisabled = 0x08u;
}

void GameGearMemoryMap::setCartridge(GameGearMapper* cartridgePtr) {
    cartridge = cartridgePtr;
}

void GameGearMemoryMap::setInput(GameGearInput* inputPtr) {
    input = inputPtr;
}

void GameGearMemoryMap::setPsg(GameGearPSG* psgPtr) {
    psg = psgPtr;
}

void GameGearMemoryMap::setVdp(GameGearVDP* vdpPtr) {
    vdp = vdpPtr;
}

uint8_t GameGearMemoryMap::readIoPort(uint8_t port) {
    if (port <= 0x05u) {
        return input ? input->readSystemPort(port) : 0xFFu;
    }
    if ((port & 0xC1u) == 0x80u) {
        return vdp ? vdp->readDataPort() : 0xFFu;
    }
    if ((port & 0xC1u) == 0x81u) {
        return vdp ? vdp->readControlPort() : 0xFFu;
    }
    if ((port & 0xC1u) == 0x40u) {
        return vdp ? vdp->readVCounter() : 0xFFu;
    }
    if ((port & 0xC1u) == 0x41u) {
        return vdp ? vdp->readHCounter() : 0xFFu;
    }
    if ((port & 0xFEu) == 0xDCu) {
        return input ? input->readInputs() : 0xFFu;
    }
    return 0xFFu;
}

void GameGearMemoryMap::writeIoPort(uint8_t port, uint8_t value) {
    if ((port >= 0x01u && port <= 0x03u) || port == 0x05u || port == 0x06u) {
        if (input) {
            input->writeSystemPort(port, value);
        }
        return;
    }
    if (port >= 0x07u && port <= 0x3Fu) {
        if ((port & 0x01u) == 0u) {
            memoryControl_ = value;
        } else {
            constexpr uint8_t kThOutputLevels = 0xA0u;
            if (vdp != nullptr && ((ioControl_ ^ value) & kThOutputLevels) != 0u) {
                vdp->latchHCounter();
            }
            ioControl_ = value;
        }
        return;
    }
    if ((port & 0xC1u) == 0x80u) {
        if (vdp) {
            vdp->writeDataPort(value);
        }
        return;
    }
    if ((port & 0xC1u) == 0x81u) {
        if (vdp) {
            vdp->writeControlPort(value);
        }
        return;
    }
    if ((port & 0xC0u) == 0x40u) {
        if (psg) {
            psg->writeData(value);
        }
        return;
    }
}


GameGearMemoryMap::GameGearMemoryMap() : cartridge(&fallbackCartridgeStorage()) {}
GameGearMemoryMap::~GameGearMemoryMap() {}

void GameGearMemoryMap::reset() {
    ram.fill(0);
    // Memory-control D3 is active-low for the optional 1 KiB Game Gear BIOS.
    memoryControl_ = bios_.empty()
        ? 0xFFu
        : static_cast<uint8_t>(0xFFu & ~kMemoryControlBiosDisabled);
    ioControl_ = 0xFFu;
    if (cartridge != nullptr) {
        cartridge->reset();
    }
}

void GameGearMemoryMap::mapRom(const uint8_t* data, size_t size) {
    auto& fallback = fallbackCartridgeStorage();
    (void)fallback.load(data, size);
    if (cartridge == nullptr || cartridge == &fallback) {
        cartridge = &fallback;
    }
}

void GameGearMemoryMap::clearRom() {
    auto& fallback = fallbackCartridgeStorage();
    (void)fallback.load(nullptr, 0u);
    if (cartridge == &fallback) {
        cartridge = &fallback;
    }
}

void GameGearMemoryMap::mapBios(const uint8_t* data, size_t size) {
    bios_.clear();
    if (data == nullptr || size == 0u) {
        return;
    }
    bios_.assign(data, data + size);
}

void GameGearMemoryMap::clearBios() {
    bios_.clear();
}

bool GameGearMemoryMap::hasBios() const noexcept {
    return !bios_.empty();
}

uint8_t GameGearMemoryMap::ioControlValue() const noexcept {
    return ioControl_;
}

uint8_t GameGearMemoryMap::memoryControlValue() const noexcept {
    return memoryControl_;
}

uint8_t GameGearMemoryMap::read(uint16_t addr) const {
    // BIOS mapping: if a BIOS is loaded and the memory-control bit D3 is active (0),
    // the BIOS occupies $0000-$03FF.
    if (addr < 0x0400u && !bios_.empty() && ((memoryControl_ & kMemoryControlBiosDisabled) == 0u)) {
        return bios_[static_cast<std::size_t>(addr) % bios_.size()];
    }
    if (cartridge != nullptr && addr < 0xC000u && cartridge->loaded()) {
        return cartridge->read(addr);
    }
    if (addr >= 0x8000u && addr < 0xA000u && vdp != nullptr) {
        return vdp->readVram(addr);
    }
    if (addr >= 0xFE00u && addr < 0xFEA0u && vdp != nullptr) {
        return vdp->readOam(addr);
    }
    if (addr >= 0xFF40u && addr <= 0xFF4Bu && vdp != nullptr) {
        return vdp->readRegister(addr);
    }
    if (addr >= 0xFF10u && addr <= 0xFF26u && psg != nullptr) {
        return psg->readCompatRegister(addr);
    }
    if (addr >= 0xFF30u && addr <= 0xFF3Fu && psg != nullptr) {
        return psg->readWaveRam(addr);
    }
    if (addr == 0xFF00u) {
        return input ? input->readInputs() : 0xFFu;
    }
    if (addr == 0x00DC) {
        return input ? input->readInputs() : 0xFF;
    }
    // 0xC000-0xDFFF: RAM (8KB)
    if (addr >= 0xC000 && addr < 0xE000) {
        return ram[addr - 0xC000];
    }
    // 0xE000-0xFFFF: RAM mirror
    if (addr >= 0xE000) {
        return ram[addr - 0xE000];
    }
    return 0xFF;
}

void GameGearMemoryMap::write(uint16_t addr, uint8_t value) {
    if (cartridge != nullptr && cartridge->handlesControlWrite(addr)) {
        cartridge->write(addr, value);
        if (addr >= 0xE000u) {
            ram[addr - 0xE000u] = value;
        }
        return;
    }
    if (cartridge != nullptr && cartridge->handlesMappedWrite(addr)) {
        cartridge->write(addr, value);
        return;
    }
    if (addr >= 0x8000u && addr < 0xA000u && vdp != nullptr) {
        vdp->writeVram(addr, value);
        return;
    }
    if (addr >= 0xFE00u && addr < 0xFEA0u && vdp != nullptr) {
        vdp->writeOam(addr, value);
        return;
    }
    if (addr >= 0xFF40u && addr <= 0xFF4Bu && vdp != nullptr) {
        vdp->writeRegister(addr, value);
        return;
    }
    if (addr >= 0xFF10u && addr <= 0xFF26u && psg != nullptr) {
        psg->writeCompatRegister(addr, value);
        return;
    }
    if (addr >= 0xFF30u && addr <= 0xFF3Fu && psg != nullptr) {
        psg->writeWaveRam(addr, value);
        return;
    }
    // 0xC000-0xDFFF: RAM (8KB)
    if (addr >= 0xC000 && addr < 0xE000) {
        ram[addr - 0xC000] = value;
        return;
    }
    // 0xE000-0xFFFF: RAM mirror
    if (addr >= 0xE000) {
        ram[addr - 0xE000] = value;
        return;
    }
}
