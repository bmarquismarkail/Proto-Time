#include "GameGearVDP.hpp"

GameGearVDP::GameGearVDP() {}
GameGearVDP::~GameGearVDP() {}

void GameGearVDP::reset() {
    vram_.fill(0u);
    oam_.fill(0u);
    registers_.fill(0u);
    pendingCycles_ = 0u;
    scanlineReadyPending_ = false;
    vblankPending_ = false;
    registers_[0x07u] = 0xFCu; // default BGP-like palette for debug renderer compatibility
}

void GameGearVDP::step(uint32_t cpuCycles) {
    if ((registers_[0] & 0x80u) == 0u) {
        registers_[4] = 0u; // LY
        pendingCycles_ = 0u;
        scanlineReadyPending_ = false;
        vblankPending_ = false;
        return;
    }

    pendingCycles_ += cpuCycles;
    while (pendingCycles_ >= kCyclesPerScanline) {
        pendingCycles_ -= kCyclesPerScanline;
        const uint8_t currentLy = registers_[4];
        if (currentLy < kVisibleScanlines) {
            scanlineReadyPending_ = true;
        }

        uint8_t nextLy = static_cast<uint8_t>(currentLy + 1u);
        if (nextLy >= kTotalScanlines) {
            nextLy = 0u;
        }
        registers_[4] = nextLy;
        if (nextLy == kVisibleScanlines) {
            vblankPending_ = true;
        }
    }
}

uint8_t GameGearVDP::readVram(uint16_t address) const {
    const auto index = static_cast<std::size_t>(address - 0x8000u);
    return index < vram_.size() ? vram_[index] : 0xFFu;
}

void GameGearVDP::writeVram(uint16_t address, uint8_t value) {
    const auto index = static_cast<std::size_t>(address - 0x8000u);
    if (index < vram_.size()) {
        vram_[index] = value;
    }
}

uint8_t GameGearVDP::readOam(uint16_t address) const {
    const auto index = static_cast<std::size_t>(address - 0xFE00u);
    return index < oam_.size() ? oam_[index] : 0xFFu;
}

void GameGearVDP::writeOam(uint16_t address, uint8_t value) {
    const auto index = static_cast<std::size_t>(address - 0xFE00u);
    if (index < oam_.size()) {
        oam_[index] = value;
    }
}

uint8_t GameGearVDP::readRegister(uint16_t address) const {
    if (address >= 0xFF40u && address <= 0xFF4Bu) {
        return registers_[static_cast<std::size_t>(address - 0xFF40u)];
    }
    return 0xFFu;
}

void GameGearVDP::writeRegister(uint16_t address, uint8_t value) {
    if (address < 0xFF40u || address > 0xFF4Bu) {
        return;
    }
    registers_[static_cast<std::size_t>(address - 0xFF40u)] = value;
    if (address == 0xFF40u && (value & 0x80u) == 0u) {
        registers_[4] = 0u;
        pendingCycles_ = 0u;
        scanlineReadyPending_ = false;
        vblankPending_ = false;
    }
}

bool GameGearVDP::takeScanlineReady() {
    const bool ready = scanlineReadyPending_;
    scanlineReadyPending_ = false;
    return ready;
}

bool GameGearVDP::takeVBlankEntered() {
    const bool ready = vblankPending_;
    vblankPending_ = false;
    return ready;
}
