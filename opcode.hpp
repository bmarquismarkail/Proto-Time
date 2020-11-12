#ifndef OPCODE_TYPES
#define OPCODE_TYPES

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <initializer_list>

#include "inst_cycle.hpp"
#include "templ/reg_base.hpp"

namespace BMMQ {

using microcodeFunc = std::function<void()>;
//////////////////////////////////
// Opcode Creation

//	First, we need a microcode class. This microcode class will hold simple functions
class Imicrocode {
    static std::map< std::string, microcodeFunc*  > v;
public:
    Imicrocode(const std::string id, microcodeFunc *func);
    const microcodeFunc *findMicrocode(const std::string id);
    void registerMicrocode(const std::string id, microcodeFunc *func);
    void operator()() const;
};

// Next, we need a group of microcodes to create an opcode
class IOpcode {
    std::vector<const Imicrocode *> microcode;

public:
    IOpcode() = default;
    IOpcode(const Imicrocode  *func);
    IOpcode(const std::string id, microcodeFunc  *func);
    IOpcode(std::initializer_list<const Imicrocode  *> list);
    void push_microcode(const Imicrocode  *func);
    template<typename AddressType, typename DataType>
    void operator()(fetchBlock<AddressType, DataType> &fb);
};

// This is where we will hold blocks of execution
template<typename regType>
class executionBlock {
	public:
		std::vector<IOpcode> &getBlock();
		const std::vector<IOpcode> &getBlock() const;
		
		CPU_Register<regType> emplaceRegister(std::string, bool );
	private:
		std::vector<IOpcode> code;
		RegisterFile<regType> file;
};
	
///////////////
// This will hold the entire instruction set.
// Not to be confused with executionBlock, 
// which will only hold snippets of instructions mostly made in this list
using OpcodeList = std::vector<IOpcode>;
///////////////
}

#include "templ/IOpcode.impl.hpp"
#include "templ/executionBlock.impl.hpp"
#endif //OPCODE_TYPES