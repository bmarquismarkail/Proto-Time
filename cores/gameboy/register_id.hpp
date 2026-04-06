#ifndef GB_REGISTER_ID_HPP
#define GB_REGISTER_ID_HPP

#include <cstdint>

#include "../../machine/RegisterId.hpp"

namespace GB {
namespace RegisterId {

inline constexpr BMMQ::RegisterId AF{"AF"};
inline constexpr BMMQ::RegisterId BC{"BC"};
inline constexpr BMMQ::RegisterId DE{"DE"};
inline constexpr BMMQ::RegisterId HL{"HL"};
inline constexpr BMMQ::RegisterId SP{"SP"};
inline constexpr BMMQ::RegisterId PC{"PC"};
inline constexpr BMMQ::RegisterId IE{"IE"};

} // namespace RegisterId

namespace Joypad {
inline constexpr std::uint8_t Right  = 0x01;
inline constexpr std::uint8_t Left   = 0x02;
inline constexpr std::uint8_t Up     = 0x04;
inline constexpr std::uint8_t Down   = 0x08;
inline constexpr std::uint8_t A      = 0x10;
inline constexpr std::uint8_t B      = 0x20;
inline constexpr std::uint8_t Select = 0x40;
inline constexpr std::uint8_t Start  = 0x80;
} // namespace Joypad
} // namespace GB

#endif // GB_REGISTER_ID_HPP
