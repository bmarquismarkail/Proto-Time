#ifndef GB_OPCODE_DECODE_HPP
#define GB_OPCODE_DECODE_HPP

#include <cstdint>

namespace GB::Decode {

enum class R8 : std::uint8_t {
    B = 0,
    C = 1,
    D = 2,
    E = 3,
    H = 4,
    L = 5,
    HLIndirect = 6,
    A = 7,
};

enum class R16 : std::uint8_t {
    BC = 0,
    DE = 1,
    HL = 2,
    SP = 3,
};

enum class R16Stack : std::uint8_t {
    BC = 0,
    DE = 1,
    HL = 2,
    AF = 3,
};

enum class Condition : std::uint8_t {
    NZ = 0,
    Z = 1,
    NC = 2,
    C = 3,
};

inline constexpr R8 decodeR8(std::uint8_t regCode) noexcept
{
    return static_cast<R8>(regCode & 0x07u);
}

inline constexpr R8 decodeR8Dest(std::uint8_t opcode) noexcept
{
    return decodeR8(static_cast<std::uint8_t>((opcode >> 3) & 0x07u));
}

inline constexpr R8 decodeR8Src(std::uint8_t opcode) noexcept
{
    return decodeR8(opcode);
}

inline constexpr R16 decodeR16(std::uint8_t opcode) noexcept
{
    return static_cast<R16>((opcode >> 4) & 0x03u);
}

inline constexpr R16Stack decodeR16Stack(std::uint8_t opcode) noexcept
{
    return static_cast<R16Stack>((opcode >> 4) & 0x03u);
}

inline constexpr Condition decodeCondition(std::uint8_t opcode) noexcept
{
    return static_cast<Condition>((opcode >> 3) & 0x03u);
}

inline constexpr bool isUndefinedBaseOpcode(std::uint8_t opcode) noexcept
{
    switch (opcode) {
    case 0xD3:
    case 0xDB:
    case 0xDD:
    case 0xE3:
    case 0xE4:
    case 0xEB:
    case 0xEC:
    case 0xED:
    case 0xF4:
    case 0xFC:
    case 0xFD:
        return true;
    default:
        return false;
    }
}

} // namespace GB::Decode

#endif // GB_OPCODE_DECODE_HPP
