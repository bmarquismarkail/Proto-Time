#include "GameGearInput.hpp"

GameGearInput::GameGearInput() {}
GameGearInput::~GameGearInput() {}

void GameGearInput::reset() {
    logicalButtons_ = 0u;
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
