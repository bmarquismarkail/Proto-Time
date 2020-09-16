#ifndef OPCODE_TYPES
#define OPCODE_TYPES

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <initializer_list>

namespace BMMQ {
//////////////////////////////////
// Opcode Creation

//	First, we need a microcode class. This microcode class will hold simple functions
class Imicrocode {
    static std::map<std::string, std::function<void()>> v;
public:

    //	searches the microcode by its id.
    //	Returns the address of the function target executed, or null if not found.
    //	Return value may be used for caching.

    const std::function<void()> *findMicrocode(const std::string id)
    {
        const auto i = v.find(id);
        if (i == v.end()) return nullptr;
        return &i->second;

    }

    void registerMicrocode(const std::string id, std::function<void()> func)
    {
        v.insert(std::make_pair (id, func) );
    }

};

std::map<std::string, std::function<void()>> Imicrocode::v;
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
    std::vector<const std::function<void()> *> microcode;

public:

    IOpcode() = default;

    IOpcode(Imicrocode& library, const std::string id)
    {
        push_microcode(library, id);
    }

    IOpcode(const std::function<void()> *func)
    {
        push_microcode(func);
    }

    IOpcode(std::initializer_list<const std::function<void()> *> list)
        : microcode(list)
    {
    }

    void push_microcode(const std::function<void()> *func)
    {
        microcode.push_back(func);
    }

    void push_microcode(Imicrocode& library, const std::string id)
    {
        const std::function<void()> *func = library.findMicrocode(id);
        if (func != nullptr)
            push_microcode(func);
    }

    void operator()()
    {
        for(auto e : microcode) {
            (*e)();
        }
    }
};
}

#endif //OPCODE_TYPES