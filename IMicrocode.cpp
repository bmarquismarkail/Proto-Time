#include "opcode.hpp"

namespace BMMQ {

std::map<std::string, microcodeFunc* > Imicrocode::v;

Imicrocode::Imicrocode(const std::string id, microcodeFunc *func)
{
    registerMicrocode(id, func);
}

const microcodeFunc* Imicrocode::findMicrocode(const std::string id)
{
    const auto i = v.find(id);
    if (i == v.end()) return nullptr;
    return i->second;

}

void Imicrocode::registerMicrocode(const std::string id, microcodeFunc *func)
{
    v.insert(std::make_pair (id, func) );
}

void Imicrocode::operator()() const
{
    for(auto e: v) {
        (*e.second)();
    }
}

}