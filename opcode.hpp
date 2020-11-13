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

template<typename regType>
using microcodeFunc = std::function<void(RegisterFile<regType>)>;
//////////////////////////////////
// Opcode Creation

//	First, we need a microcode class. This microcode class will hold simple functions
template<typename regType>
class Imicrocode {
    static std::map< std::string, microcodeFunc<regType>*  > v;
public:
    Imicrocode(const std::string id, microcodeFunc<regType> *func);
    const microcodeFunc<regType> *findMicrocode(const std::string id);
    void registerMicrocode(const std::string id, microcodeFunc<regType> *func);
    void operator()(const RegisterFile<regType>& file) const;
};

// Next, we need a group of microcodes to create an opcode
template<typename regType>
class IOpcode {
    std::vector<const Imicrocode<regType>*> microcode;

public:
    IOpcode() = default;
    IOpcode(const Imicrocode<regType>  *func);
    IOpcode(const std::string id, microcodeFunc<regType>  *func);
    IOpcode(std::initializer_list<const Imicrocode<regType>  *> list);
    void push_microcode(const Imicrocode<regType>  *func);
	void operator()(const RegisterFile<regType>& file);
};

// This is where we will hold blocks of execution
template<typename regType>
class executionBlock {
	public:
		std::vector<IOpcode<regType>> &getBlock();
		const std::vector<IOpcode<regType>> &getBlock() const;
		CPU_Register<regType>& emplaceRegister(std::string_view, bool );
		const RegisterFile<regType>& getRegisterFile() const;
	private:
		std::vector<IOpcode<regType>> code;
		RegisterFile<regType> file;
};
	
///////////////
// This will hold the entire instruction set.
// Not to be confused with executionBlock, 
// which will only hold snippets of instructions mostly made in this list
template<typename regType>
	using OpcodeList = std::vector<IOpcode<regType>>;
///////////////
}

#include "templ/IMicrocode.impl.hpp"
#include "templ/IOpcode.impl.hpp"
#include "templ/executionBlock.impl.hpp"
#endif //OPCODE_TYPES