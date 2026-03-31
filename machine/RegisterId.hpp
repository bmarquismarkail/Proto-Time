#ifndef BMMQ_REGISTER_ID_HPP
#define BMMQ_REGISTER_ID_HPP

#include <stdexcept>
#include <string_view>

namespace BMMQ {

enum class RegisterId {
    AF,
    BC,
    DE,
    HL,
    SP,
    PC,
    MDR,
    IME,
};

constexpr std::string_view toString(RegisterId id) noexcept
{
    switch (id) {
    case RegisterId::AF:
        return "AF";
    case RegisterId::BC:
        return "BC";
    case RegisterId::DE:
        return "DE";
    case RegisterId::HL:
        return "HL";
    case RegisterId::SP:
        return "SP";
    case RegisterId::PC:
        return "PC";
    case RegisterId::MDR:
        return "MDR";
    case RegisterId::IME:
        return "IME";
    }
    return "unknown";
}

inline RegisterId registerIdFromString(std::string_view name)
{
    if (name == "AF") return RegisterId::AF;
    if (name == "BC") return RegisterId::BC;
    if (name == "DE") return RegisterId::DE;
    if (name == "HL") return RegisterId::HL;
    if (name == "SP") return RegisterId::SP;
    if (name == "PC") return RegisterId::PC;
    if (name == "mdr" || name == "MDR") return RegisterId::MDR;
    if (name == "ime" || name == "IME") return RegisterId::IME;
    throw std::invalid_argument("unknown register id");
}

} // namespace BMMQ

#endif // BMMQ_REGISTER_ID_HPP
