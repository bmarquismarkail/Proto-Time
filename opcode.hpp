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
    static std::map< std::string, microcodeFunc*  > v;
public:
	
	Imicrocode(const std::string id, microcodeFunc *func){
		registerMicrocode(id, func);
	}
	
    //	searches the microcode by its id.
    //	Returns the address of the function target executed, or null if not found.
    //	Return value may be used for caching.

    const microcodeFunc *findMicrocode(const std::string id)
    {
        const auto i = v.find(id);
        if (i == v.end()) return nullptr;
        return i->second;

    }

    void registerMicrocode(const std::string id, microcodeFunc *func)
    {
        v.insert(std::make_pair (id, func) );
    }
	
	void operator()() const {
		for(auto e: v){
			(*e.second)();
		}
	}
};

std::map<std::string, microcodeFunc* > Imicrocode::v;


// Next, we need a group of microcodes to create an opcode
class IOpcode {
    // list of functions
    std::vector<const Imicrocode *> microcode;

public:

    IOpcode() = default;

    IOpcode(const Imicrocode  *func)
    {
        push_microcode(func);
    }

    IOpcode(const std::string id, microcodeFunc  *func)
    {
        push_microcode(new Imicrocode(id, func) );
    }

    IOpcode(std::initializer_list<const Imicrocode  *> list)
        : microcode(list)
    {
    }

    void push_microcode(const Imicrocode  *func)
    {
        microcode.push_back(func);
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