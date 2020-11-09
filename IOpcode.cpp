#include "opcode.hpp"
namespace BMMQ {

IOpcode::IOpcode(const Imicrocode  *func)
{
    push_microcode(func);
}

IOpcode::IOpcode(const std::string id, microcodeFunc  *func)
{
    push_microcode(new Imicrocode(id, func) );
}

IOpcode::IOpcode(std::initializer_list<const Imicrocode  *> list)
    : microcode(list)
{
}

void IOpcode::push_microcode(const Imicrocode  *func)
{
    microcode.push_back(func);
}
}