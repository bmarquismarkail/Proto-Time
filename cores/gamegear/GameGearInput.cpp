#include "GameGearInput.hpp"

GameGearInput::GameGearInput() {}
GameGearInput::~GameGearInput() {}

void GameGearInput::reset() {
    logicalButtons_ = 0u;
    extData_ = 0x7Fu;
    extDirectionNmi_ = 0xFFu;
    serialTxData_ = 0u;
    serialRxData_ = 0xFFu;
    serialControl_ = 0u;
    audioStereoControl_ = 0xFFu;
}

void GameGearInput::setLogicalButtons(BMMQ::InputButtonMask mask) {
    logicalButtons_ = mask;
}

BMMQ::InputButtonMask GameGearInput::logicalButtons() const noexcept {
    return logicalButtons_;
}

uint8_t GameGearInput::readInputs() {
    // Game Gear input port bits are active-low.
    uint8_t state = 0xFFu;
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Up)) != 0u) {
        state &= static_cast<uint8_t>(~0x01u);
    }
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Down)) != 0u) {
        state &= static_cast<uint8_t>(~0x02u);
    }
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Left)) != 0u) {
        state &= static_cast<uint8_t>(~0x04u);
    }
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Right)) != 0u) {
        state &= static_cast<uint8_t>(~0x08u);
    }
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Button1)) != 0u) {
        state &= static_cast<uint8_t>(~0x10u);
    }
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Button2)) != 0u) {
        state &= static_cast<uint8_t>(~0x20u);
    }
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Meta1)) != 0u) {
        state &= static_cast<uint8_t>(~0x80u);
    }
    return state;
}

uint8_t GameGearInput::readSystemPort0() const noexcept {
    uint8_t state = 0x40u; // Overseas + NTSC default, low bits read as zero.
    if ((logicalButtons_ & BMMQ::inputButtonMask(BMMQ::InputButton::Meta2)) == 0u) {
        state = static_cast<uint8_t>(state | 0x80u);
    }
    return state;
}

uint8_t GameGearInput::readSystemPort(uint8_t port) const noexcept {
    switch (port) {
    case 0x00u:
        return readSystemPort0();
    case 0x01u:
        return extData_;
    case 0x02u:
        return extDirectionNmi_;
    case 0x03u:
        return serialTxData_;
    case 0x04u:
        return serialRxData_;
    case 0x05u:
        return serialControl_;
    default:
        return 0xFFu;
    }
}

void GameGearInput::writeSystemPort(uint8_t port, uint8_t value) noexcept {
    switch (port) {
    case 0x01u:
        extData_ = value;
        break;
    case 0x02u:
        extDirectionNmi_ = value;
        break;
    case 0x03u:
        serialTxData_ = value;
        break;
    case 0x05u:
        serialControl_ = value;
        break;
    case 0x06u:
        audioStereoControl_ = value;
        break;
    default:
        break;
    }
}

uint8_t GameGearInput::audioStereoControl() const noexcept {
    return audioStereoControl_;
}
