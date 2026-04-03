#ifndef GB_REGISTER_ID_HPP
#define GB_REGISTER_ID_HPP

#include "../../machine/RegisterId.hpp"

namespace GB {
namespace RegisterId {

inline constexpr BMMQ::RegisterId AF{"AF"};
inline constexpr BMMQ::RegisterId BC{"BC"};
inline constexpr BMMQ::RegisterId DE{"DE"};
inline constexpr BMMQ::RegisterId HL{"HL"};
inline constexpr BMMQ::RegisterId SP{"SP"};
inline constexpr BMMQ::RegisterId PC{"PC"};
inline constexpr BMMQ::RegisterId IME{"IME"};

} // namespace RegisterId
} // namespace GB

#endif // GB_REGISTER_ID_HPP
