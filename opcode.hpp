#ifndef OPCODE_TYPES
#define OPCODE_TYPES

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <initializer_list>

#include "inst_cycle.hpp"

namespace BMMQ {

template<typename AddressType, typename DataType>
using microcodeFunc = std::function<void(fetchBlock<AddressType, DataType>)>;
//////////////////////////////////
// Opcode Creation

//	First, we need a microcode class. This microcode class will hold simple functions
template<typename AddressType, typename DataType>
class Imicrocode {
    static std::map< std::string, microcodeFunc<AddressType, DataType> > v;
public:

    //	searches the microcode by its id.
    //	Returns the address of the function target executed, or null if not found.
    //	Return value may be used for caching.

    const microcodeFunc<AddressType, DataType> *findMicrocode(const std::string id)
    {
        const auto i = v.find(id);
        if (i == v.end()) return nullptr;
        return &i->second;

    }

    void registerMicrocode(const std::string id, microcodeFunc<AddressType, DataType> func)
    {
        v.insert(std::make_pair (id, func) );
    }

};

template<typename AddressType, typename DataType>
std::map<std::string, microcodeFunc<AddressType, DataType>> Imicrocode<AddressType, DataType>::v;
// Let's make a class derived from the microcode struct and add a function
// A common microcode library will be made for convenience,
// But the implementer will be able to make microcode functions is required or desired.

template<typename AddressType, typename DataType>
class derivedMicrocode: public Imicrocode<AddressType, DataType> {
public:
    template<typename T>
    void iadd(T a, T b)
    {
        std::cout << a+b << '\n';
    }
};

// Next, we need a group of microcodes to create an opcode
template<typename AddressType, typename DataType>
class IOpcode {
    // list of functions
    std::vector<const microcodeFunc<AddressType, DataType> *> microcode;

public:

    IOpcode() = default;

    IOpcode(Imicrocode<AddressType, DataType>& library, const std::string id)
    {
        push_microcode(library, id);
    }

    IOpcode(const microcodeFunc<AddressType, DataType> *func)
    {
        push_microcode(func);
    }

    IOpcode(std::initializer_list<const microcodeFunc<AddressType, DataType> *> list)
        : microcode(list)
    {
    }

    void push_microcode(const microcodeFunc<AddressType, DataType> *func)
    {
        microcode.push_back(func);
    }

    void push_microcode(Imicrocode<AddressType, DataType>& library, const std::string id)
    {
        const microcodeFunc<AddressType, DataType> *func = library.findMicrocode(id);
        if (func != nullptr)
            push_microcode(func);
    }

    void operator()(fetchBlock<AddressType, DataType> &fb)
    {
        for(auto e : microcode) {
            (*e)(fb);
        }
    }
};
///////////////
template<typename AddressType, typename DataType>
using OpcodeList = std::vector<IOpcode<AddressType, DataType> >;

template<typename AddressType, typename DataType>
using executionBlock = std::vector<std::pair<DataType, IOpcode<AddressType, DataType> >>;
///////////////
}

#endif //OPCODE_TYPES