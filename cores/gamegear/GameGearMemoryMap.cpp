#include "GameGearMemoryMap.hpp"

#include "GameGearCartridge.hpp"
#include "GameGearInput.hpp"
#include "GameGearPSG.hpp"
#include "GameGearVDP.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
GameGearCartridge& fallbackCartridgeStorage() {
    static GameGearCartridge cartridge;
    return cartridge;
}

constexpr uint8_t kMemoryControlBiosDisabled = 0x08u;
}

void GameGearMemoryMap::setCartridge(GameGearMapper* cartridgePtr) {
    cartridge = cartridgePtr;
    ++codeMappingGeneration_;
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
        if (port == 0x06u && psg) {
            psg->writeStereoControl(value);
        }
        return;
    }
    if (port >= 0x07u && port <= 0x3Fu) {
        if ((port & 0x01u) == 0u) {
            if (memoryControl_ != value) ++codeMappingGeneration_;
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

void appendU32(std::vector<uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
}

std::uint32_t readU32(const std::vector<uint8_t>& bytes, std::size_t& pos)
{
    if (pos > bytes.size() || bytes.size() - pos < 4u) {
        throw std::invalid_argument("Game Gear memory state truncated");
    }
    const auto value = static_cast<std::uint32_t>(bytes[pos]) |
        (static_cast<std::uint32_t>(bytes[pos + 1u]) << 8u) |
        (static_cast<std::uint32_t>(bytes[pos + 2u]) << 16u) |
        (static_cast<std::uint32_t>(bytes[pos + 3u]) << 24u);
    pos += 4u;
    return value;
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
    ++codeMappingGeneration_;
}

void GameGearMemoryMap::mapRom(const uint8_t* data, size_t size) {
    auto& fallback = fallbackCartridgeStorage();
    (void)fallback.load(data, size);
    if (cartridge == nullptr || cartridge == &fallback) {
        cartridge = &fallback;
    }
    ++codeMappingGeneration_;
}

void GameGearMemoryMap::clearRom() {
    auto& fallback = fallbackCartridgeStorage();
    (void)fallback.load(nullptr, 0u);
    if (cartridge == &fallback) {
        cartridge = &fallback;
    }
    ++codeMappingGeneration_;
}

void GameGearMemoryMap::mapBios(const uint8_t* data, size_t size) {
    bios_.clear();
    ++codeMappingGeneration_;
    if (data == nullptr || size == 0u) {
        return;
    }
    bios_.assign(data, data + size);
}

void GameGearMemoryMap::clearBios() {
    bios_.clear();
    ++codeMappingGeneration_;
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

uint64_t GameGearMemoryMap::codeMappingGeneration() const noexcept {
    return codeMappingGeneration_;
}

uint8_t GameGearMemoryMap::read(uint16_t addr) const {
    // BIOS mapping: if a BIOS is loaded and the memory-control bit D3 is active (0),
    // the BIOS occupies $0000-$03FF.
    if (addr < 0x0400u && !bios_.empty() && ((memoryControl_ & kMemoryControlBiosDisabled) == 0u)) {
        return bios_[static_cast<std::size_t>(addr) % bios_.size()];
    }
    if (addr == 0x00DCu || addr == 0x00DDu) {
        return input ? input->readInputs() : 0xFFu;
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
    // 0xC000-0xDFFF: RAM (8KB)
    if (addr >= 0xC000 && addr < 0xE000) {
        return ram[addr - 0xC000];
    }
    // 0xE000-0xFFFF: RAM mirror
    if (addr >= 0xE000) {
        return ram[addr - 0xE000];
    }
    // RAM mirror for $FFFC-$FFFF and $DFFC-$DFFF
    if ((addr >= 0xDFFCu && addr <= 0xDFFFu) || (addr >= 0xFFFCu)) {
        return ram[0x1FFCu + (addr & 0x3)];
    }
    return 0xFF;
}

bool GameGearMemoryMap::peekCodeByte(uint16_t addr, uint8_t& value) const noexcept {
    // Reject compatibility MMIO and mirrored high-memory device windows. Code
    // peeks must not acknowledge input, video, or audio state.
    if (addr == 0x00DCu || addr == 0x00DDu || addr >= 0xFE00u ||
        (addr >= 0x8000u && addr < 0xA000u && cartridge == nullptr)) {
        return false;
    }
    if (addr < 0x0400u && !bios_.empty() &&
        ((memoryControl_ & kMemoryControlBiosDisabled) == 0u)) {
        value = bios_[static_cast<std::size_t>(addr) % bios_.size()];
        return true;
    }
    if (addr < 0xC000u) {
        if (cartridge == nullptr || !cartridge->loaded()) return false;
        value = cartridge->read(addr);
        return true;
    }
    if (addr < 0xFE00u) {
        value = addr < 0xE000u ? ram[addr - 0xC000u] : ram[addr - 0xE000u];
        return true;
    }
    return false;
}

void GameGearMemoryMap::write(uint16_t addr, uint8_t value) {
    // Mapper register region: $FFFC-$FFFF
    if (cartridge != nullptr && cartridge->handlesControlWrite(addr)) {
        cartridge->write(addr, value);
        ++codeMappingGeneration_;
        // Writes to $FFFC-$FFFF also update RAM mirror at $1FFC-$1FFF
        if (addr >= 0xFFFCu) {
            ram[0x1FFCu + (addr & 0x3)] = value;
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
    // Writes to $DFFC-$DFFF also update RAM mirror at $1FFC-$1FFF
    if (addr >= 0xDFFCu && addr <= 0xDFFFu) {
        ram[0x1FFCu + (addr & 0x3)] = value;
        return;
    }
}

std::vector<uint8_t> GameGearMemoryMap::exportState() const {
    std::vector<uint8_t> state;
    state.reserve(2u + ram.size() + bios_.size() + 8u);
    state.push_back(memoryControl_);
    state.push_back(ioControl_);
    appendU32(state, static_cast<std::uint32_t>(ram.size()));
    state.insert(state.end(), ram.begin(), ram.end());
    appendU32(state, static_cast<std::uint32_t>(bios_.size()));
    state.insert(state.end(), bios_.begin(), bios_.end());
    return state;
}

void GameGearMemoryMap::importState(const std::vector<uint8_t>& state) {
    std::size_t pos = 0;
    if (state.size() < 10u) {
        throw std::invalid_argument("Game Gear memory state too short");
    }
    const auto nextMemoryControl = state[pos++];
    const auto nextIoControl = state[pos++];
    const auto ramSize = static_cast<std::size_t>(readU32(state, pos));
    if (ramSize != ram.size() || state.size() - pos < ramSize + 4u) {
        throw std::invalid_argument("Game Gear memory state RAM size mismatch");
    }
    decltype(ram) nextRam{};
    std::copy_n(state.begin() + static_cast<std::ptrdiff_t>(pos), nextRam.size(), nextRam.begin());
    pos += nextRam.size();
    const auto biosSize = static_cast<std::size_t>(readU32(state, pos));
    if (biosSize > 0x4000u || state.size() - pos != biosSize) {
        throw std::invalid_argument("Game Gear memory state BIOS size invalid");
    }
    std::vector<uint8_t> nextBios(state.begin() + static_cast<std::ptrdiff_t>(pos), state.end());

    memoryControl_ = nextMemoryControl;
    ioControl_ = nextIoControl;
    ram = nextRam;
    bios_ = std::move(nextBios);
    ++codeMappingGeneration_;
}
