#ifndef OPCODE_TYPES
#define OPCODE_TYPES

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <initializer_list>

#include "inst_cycle.hpp"

namespace BMMQ {

using microcodeFunc = std::function<void()>;
//////////////////////////////////
// Opcode Creation

//	First, we need a microcode class. This microcode class will hold simple functions
class Imicrocode {
    static std::map< std::string, microcodeFunc  > v;
public:

    //	searches the microcode by its id.
    //	Returns the address of the function target executed, or null if not found.
    //	Return value may be used for caching.

    const microcodeFunc *findMicrocode(const std::string id)
    {
        const auto i = v.find(id);
        if (i == v.end()) return nullptr;
        return &i->second;

    }

    void registerMicrocode(const std::string id, microcodeFunc func)
    {
        v.insert(std::make_pair (id, func) );
    }

};

std::map<std::string, microcodeFunc > Imicrocode::v;
// Let's make a class derived from the microcode struct and add a function
// A common microcode library will be made for convenience,
// But the implementer will be able to make microcode functions is required or desired.

class derivedMicrocode: public Imicrocode {
public:
    template<typename T>
    void iadd(T a, T b)
    {
        std::cout << a+b << '\n';
    }
};

// Next, we need a group of microcodes to create an opcode
class IOpcode {
    // list of functions
    std::vector<const microcodeFunc *> microcode;

public:

    IOpcode() = default;

    IOpcode(Imicrocode& library, const std::string id)
    {
        push_microcode(library, id);
    }

    IOpcode(const microcodeFunc  *func)
    {
        push_microcode(func);
    }

    IOpcode(std::initializer_list<const microcodeFunc  *> list)
        : microcode(list)
    {
    }

    void push_microcode(const microcodeFunc  *func)
    {
        microcode.push_back(func);
    }

    void push_microcode(Imicrocode& library, const std::string id)
    {
        const microcodeFunc  *func = library.findMicrocode(id);
        if (func != nullptr)
            push_microcode(func);
    }
	
	template<typename AddressType, typename DataType>
    void operator()(fetchBlock<AddressType, DataType> &fb)
    {
        for(auto e : microcode) {
            (*e)();
        }
    }
};
///////////////
using OpcodeList = std::vector<IOpcode>;

using executionBlock = std::vector<IOpcode>;
///////////////
}

#endif //OPCODE_TYPES