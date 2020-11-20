#ifndef OPCODE_TYPES
#define OPCODE_TYPES

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <initializer_list>

#include "inst_cycle.hpp"
#include "memory/reg_base.hpp"
#include "memory/MemoryPool.hpp"

namespace BMMQ {

/*template<typename regType>
using microcodeFunc = std::function<void(RegisterFile<regType>)>;*/

template<typename AddressType, typename DataType, typename RegType>
using microcodeFunc = std::function<void(MemoryPool<AddressType, DataType, RegType>)>;
//////////////////////////////////
// Opcode Creation

//	First, we need a microcode class. This microcode class will hold simple functions
template<typename AddressType, typename DataType, typename RegType>
class Imicrocode {
    static std::map< std::string, microcodeFunc<AddressType, DataType, RegType>*  > v;
public:
    Imicrocode(const std::string id, microcodeFunc<AddressType, DataType, RegType> *func);
    const microcodeFunc<AddressType, DataType, RegType> *findMicrocode(const std::string id);
    void registerMicrocode(const std::string id, microcodeFunc<AddressType, DataType, RegType> *func);
    void operator()(const MemoryPool<AddressType, DataType, RegType>& file) const;
};

// Next, we need a group of microcodes to create an opcode
template<typename AddressType, typename DataType, typename RegType>
class IOpcode {
    std::vector<const Imicrocode<AddressType, DataType, RegType>*> microcode;

public:
    IOpcode() = default;
    IOpcode(const Imicrocode<AddressType, DataType, RegType>  *func);
    IOpcode(const std::string id, microcodeFunc<AddressType, DataType, RegType>  *func);
    IOpcode(std::initializer_list<const Imicrocode<AddressType, DataType, RegType>  *> list);
    void push_microcode(const Imicrocode<AddressType, DataType, RegType>  *func);
    void operator()(const MemoryPool<AddressType, DataType, RegType>& file);
};

// This is where we will hold blocks of execution
template<typename AddressType, typename DataType, typename RegType>
class executionBlock {
public:
    std::vector<IOpcode<AddressType, DataType, RegType>> &getBlock();
    const std::vector<IOpcode<AddressType, DataType, RegType>> &getBlock() const;
    CPU_Register<RegType>& emplaceRegister(std::string_view, bool );
    const MemoryPool<AddressType, DataType, RegType>& getMemory() const;
private:
    std::vector<IOpcode<AddressType, DataType, RegType>> code;
    MemoryPool<AddressType, DataType, RegType> mem;
};

///////////////
// This will hold the entire instruction set.
// Not to be confused with executionBlock,
// which will only hold snippets of instructions mostly made in this list
template<typename AddressType, typename DataType, typename RegType>
using OpcodeList = std::vector<IOpcode<AddressType, DataType, RegType>>;
///////////////
}

#include "templ/IMicrocode.impl.hpp"
#include "templ/IOpcode.impl.hpp"
#include "templ/executionBlock.impl.hpp"
#endif //OPCODE_TYPES