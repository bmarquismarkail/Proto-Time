#ifndef BMMQ_REGISTER_ID_HPP
#define BMMQ_REGISTER_ID_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace BMMQ {

struct RegisterId {
    std::string_view value {};

    constexpr RegisterId() noexcept = default;
    constexpr RegisterId(std::string_view v) noexcept
        : value(v) {}

    constexpr operator std::string_view() const noexcept
    {
        return value;
    }

    static const RegisterId AF;
    static const RegisterId BC;
    static const RegisterId DE;
    static const RegisterId HL;
    static const RegisterId SP;
    static const RegisterId PC;
    static const RegisterId IME;
};

enum class RegisterWidth {
    Byte8,
    Word16,
};

enum class RegisterStorage {
    RegisterFile,
    AddressMapped,
};

struct RegisterDescriptor {
    std::string name;
    RegisterWidth width = RegisterWidth::Word16;
    RegisterStorage storage = RegisterStorage::RegisterFile;
    std::optional<uint16_t> mappedAddress {};
    bool isPair = false;
};

inline constexpr std::string_view registerNameOf(const RegisterDescriptor& descriptor) noexcept
{
    return descriptor.name;
}

inline constexpr RegisterId RegisterId::AF{"AF"};
inline constexpr RegisterId RegisterId::BC{"BC"};
inline constexpr RegisterId RegisterId::DE{"DE"};
inline constexpr RegisterId RegisterId::HL{"HL"};
inline constexpr RegisterId RegisterId::SP{"SP"};
inline constexpr RegisterId RegisterId::PC{"PC"};
inline constexpr RegisterId RegisterId::IME{"IME"};

} // namespace BMMQ

#endif // BMMQ_REGISTER_ID_HPP
