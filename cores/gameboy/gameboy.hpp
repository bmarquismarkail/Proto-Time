#ifndef DMG_CPU
#define DMG_CPU

#include <stdexcept>
#include <cstdio>

#include "../../inst_cycle/execute/opcode.hpp"
#include "../../inst_cycle/fetch/fetchBlock.hpp"
#include "../../CPU.hpp"
#include "../../common_microcode.hpp"
#include "../../memory/templ/reg_uint16.impl.hpp"
#include "../../memory/MemoryPool.hpp"
#include "../../memory/MemorySnapshot/MemorySnapshot.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;
using LR3592_RegisterFile = BMMQ::RegisterFile<AddressType>;

class LR3592_DMG : public BMMQ::CPU<AddressType, DataType, AddressType> {
    //BMMQ::OpcodeList<AddressType, DataType, AddressType> opcodeList;
    BMMQ::MemoryPool<AddressType, DataType, AddressType> mem;
    LR3592_Register mar;
    LR3592_RegisterPair mdr;
    uint16_t flagset;
    DataType cip;
    bool ime = false, stopFlag = false;

    //Gameboy-specific Decode Helper Functions
	LR3592_Register &GetRegister(BMMQ::RegisterInfo<AddressType>& Reg, LR3592_RegisterFile* file);
    bool checkJumpCond(DataType opcode, LR3592_RegisterFile* file);
    AddressType* ld_R16_I16_GetRegister(DataType opcode, LR3592_RegisterFile *File);
    AddressType* add_HL_r16_GetRegister(DataType opcode, LR3592_RegisterFile* file);
    AddressType* ld_r16_8_GetRegister(DataType opcode, LR3592_RegisterFile* file);
    std::pair<DataType*, DataType*>  ld_r16_8_GetOperands(DataType opcode, LR3592_RegisterFile* file);
    DataType* ld_r8_i8_GetRegister(DataType opcode, LR3592_RegisterFile* file);
    void rotateAccumulator(DataType opcode, LR3592_RegisterFile* file);
    void manipulateAccumulator(DataType opcode, LR3592_RegisterFile* file);
    void manipulateCarry(DataType opcode, LR3592_RegisterFile* file);
    DataType* ld_r8_r8_GetRegister(DataType regcode, LR3592_RegisterFile* file);
    std::pair<DataType*, DataType*> ld_r8_r8_GetOperands(DataType opcode, LR3592_RegisterFile* file);
    void math_r8(DataType opcode, LR3592_RegisterFile* file);
    void math_i8(DataType opcode, LR3592_RegisterFile* file);
    void ret(LR3592_RegisterFile* file);
    void ret_cc(DataType opcode, LR3592_RegisterFile* file);
    AddressType* push_pop_GetRegister(DataType opcode, LR3592_RegisterFile* file);
    void pop(DataType opcode, LR3592_RegisterFile* file);
    void push(DataType opcode, LR3592_RegisterFile* file);
    void call(LR3592_RegisterFile* file);
    void call_cc(DataType opcode, LR3592_RegisterFile* file);
    void rst(DataType opcode, LR3592_RegisterFile* file);
    void ldh(DataType opcode, LR3592_RegisterFile* file);
    void ld_ir16_r8(DataType opcode, LR3592_RegisterFile* file);
    void ei_di(DataType opcode, LR3592_RegisterFile* file);
    void ld_hl_sp(DataType opcode, LR3592_RegisterFile* file);
    void cb_execute(DataType opcode, LR3592_RegisterFile* file);
    void calculateflags(uint16_t calculationFlags, LR3592_RegisterFile* file);
    void nop();
    void stop();
    void populateOpcodes();
public:
    BMMQ::RegisterInfo<AddressType> AF, BC, DE, HL, SP, PC;

    //CTOR

    LR3592_DMG();
    BMMQ::MemoryStorage<AddressType, DataType> buildMemoryStore();
    LR3592_RegisterFile buildRegisterfile();
    BMMQ::fetchBlock<AddressType, DataType> fetch();
    BMMQ::executionBlock<AddressType, DataType, AddressType> decode(BMMQ::OpcodeList<AddressType, DataType, AddressType> &oplist, BMMQ::fetchBlock<AddressType, DataType>& fetchData);
    void execute(const BMMQ::executionBlock<AddressType, DataType, AddressType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb );
	BMMQ::MemoryPool<AddressType, DataType, AddressType>& getMemory();
	void setStopFlag(bool f);
};
#endif //DMG_CPU
