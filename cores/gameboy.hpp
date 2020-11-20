#ifndef DMG_CPU
#define DMG_CPU

#include <stdexcept>
#include <cstdio>

#include "../opcode.hpp"
#include "../inst_cycle.hpp"
#include "../CPU.hpp"
#include "../common_microcode.hpp"
#include "../memory/templ/reg_uint16.impl.hpp"
#include "../memory/MemoryPool.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;

class LR3592_DMG : public BMMQ::CPU<AddressType, DataType, AddressType> {
    BMMQ::OpcodeList<AddressType, DataType, AddressType> opcodeList;
    BMMQ::MemoryPool<AddressType, DataType, AddressType> mem;
    LR3592_Register mar;
    LR3592_RegisterPair mdr;
    uint16_t flagset;
    DataType cip;
    bool ime = false, stopFlag = false;

	//Gameboy-specific Decode Helper Functions
	bool checkJumpCond(DataType opcode);
	AddressType* ld_R16_I16_GetRegister(DataType opcode);
	AddressType* add_HL_r16_GetRegister(DataType opcode);
	AddressType* ld_r16_8_GetRegister(DataType opcode);
	std::pair<DataType*, DataType*>  ld_r16_8_GetOperands(DataType opcode);
	DataType* ld_r8_i8_GetRegister(DataType opcode);
	void rotateAccumulator(DataType opcode);
	void manipulateAccumulator(DataType opcode);
	void manipulateCarry(DataType opcode);
	DataType* ld_r8_r8_GetRegister(DataType regcode);
	std::pair<DataType*, DataType*> ld_r8_r8_GetOperands(DataType opcode);
	void math_r8(DataType opcode);
	void math_i8(DataType opcode);
	void ret();
	void ret_cc(DataType opcode);
	AddressType* push_pop_GetRegister(DataType opcode);
	void pop(DataType opcode);
	void push(DataType opcode);
	void call();
	void call_cc(DataType opcode);
	void rst(DataType opcode);
	void ldh(DataType opcode);
	void ld_ir16_r8(DataType opcode);
	void ei_di(DataType opcode);
	void ld_hl_sp(DataType opcode);
	void cb_execute(DataType opcode);
	void calculateflags(uint16_t calculationFlags);
	void nop();
	void stop();
	void populateOpcodes();
public:
    BMMQ::RegisterInfo<AddressType> AF, BC, DE, HL, SP, PC;

    //CTOR

    LR3592_DMG();
    BMMQ::MemoryMap<AddressType, DataType> buildMemoryMap();
    BMMQ::RegisterFile<AddressType> buildRegisterfile();
    BMMQ::fetchBlock<AddressType, DataType> fetch();
    BMMQ::executionBlock<AddressType, DataType, AddressType> decode(BMMQ::OpcodeList<AddressType, DataType, AddressType> &oplist, BMMQ::fetchBlock<AddressType, DataType>& fetchData);
    void execute(const BMMQ::executionBlock<AddressType, DataType, AddressType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb );
};
#endif //DMG_CPU
