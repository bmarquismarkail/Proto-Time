#ifndef BMMQ_INPUT_TYPES_HPP
#define BMMQ_INPUT_TYPES_HPP

#include <cstdint>

namespace BMMQ {

enum class InputButton : uint8_t {
    Right = 0x01u,
    Left = 0x02u,
    Up = 0x04u,
    Down = 0x08u,
    Button1 = 0x10u,
    Button2 = 0x20u,
    Meta1 = 0x40u,
    Meta2 = 0x80u,
};

using InputButtonMask = uint8_t;

[[nodiscard]] constexpr InputButtonMask inputButtonMask(InputButton button) noexcept
{
    return static_cast<InputButtonMask>(button);
}

} // namespace BMMQ

#endif // BMMQ_INPUT_TYPES_HPP
