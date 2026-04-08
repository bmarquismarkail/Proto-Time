#ifndef _TEMP_UINT16
#define _TEMP_UINT16

#include "../reg_base.hpp"

#include <bit>
#include <cstdint>
#include <memory>

namespace BMMQ {

class RegisterByteRef {
public:
    RegisterByteRef(uint16_t& value, bool highByte) noexcept
        : value_(&value), highByte_(highByte) {}

    RegisterByteRef& operator=(uint8_t rhs) noexcept
    {
        const auto preserved = static_cast<uint16_t>(highByte_ ? (*value_ & 0x00FFu) : (*value_ & 0xFF00u));
        *value_ = static_cast<uint16_t>(preserved | (highByte_ ? static_cast<uint16_t>(rhs) << 8 : rhs));
        return *this;
    }

    RegisterByteRef& operator+=(uint8_t rhs) noexcept
    {
        *this = static_cast<uint8_t>(*this) + rhs;
        return *this;
    }

    RegisterByteRef& operator-=(uint8_t rhs) noexcept
    {
        *this = static_cast<uint8_t>(*this) - rhs;
        return *this;
    }

    RegisterByteRef& operator&=(uint8_t rhs) noexcept
    {
        *this = static_cast<uint8_t>(*this) & rhs;
        return *this;
    }

    RegisterByteRef& operator|=(uint8_t rhs) noexcept
    {
        *this = static_cast<uint8_t>(*this) | rhs;
        return *this;
    }

    RegisterByteRef& operator^=(uint8_t rhs) noexcept
    {
        *this = static_cast<uint8_t>(*this) ^ rhs;
        return *this;
    }

    operator uint8_t() const noexcept
    {
        return highByte_ ? static_cast<uint8_t>((*value_ >> 8) & 0x00FFu)
                         : static_cast<uint8_t>(*value_ & 0x00FFu);
    }

    uint8_t* operator&() noexcept
    {
        return rawPointer();
    }

    const uint8_t* operator&() const noexcept
    {
        return rawPointer();
    }

private:
    uint8_t* rawPointer() const noexcept
    {
        auto* bytes = reinterpret_cast<uint8_t*>(value_);
        constexpr bool isBigEndian = (std::endian::native == std::endian::big);
        const std::size_t offset = highByte_ ^ isBigEndian ? 1 : 0;
        return bytes + offset;
    }

    uint16_t* value_;
    bool highByte_;
};

template<>
struct CPU_RegisterPair<uint16_t> : public CPU_Register<uint16_t> {
    RegisterByteRef lo;
    RegisterByteRef hi;

    CPU_RegisterPair()
        : lo(this->value, false), hi(this->value, true) {}

    CPU_RegisterPair(const CPU_RegisterPair& other)
        : CPU_Register<uint16_t>(other), lo(this->value, false), hi(this->value, true) {}

    CPU_RegisterPair& operator=(const CPU_RegisterPair& other)
    {
        if (this == &other) return *this;
        this->value = other.value;
        return *this;
    }

    uint16_t operator()() const override
    {
        return this->value;
    }

    bool isPair() const noexcept override
    {
        return true;
    }

    std::unique_ptr<CPU_Register<uint16_t>> clone() const override
    {
        return std::make_unique<CPU_RegisterPair<uint16_t>>(*this);
    }
};

} // namespace BMMQ

#endif //_TEMP_UINT16
