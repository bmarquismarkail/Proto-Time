#ifndef DMG_CPU
#define DMG_CPU

#include <stdexcept>
#include <cstdio>

#include "../opcode.hpp"
#include "../inst_cycle.hpp"
#include "../cpu.hpp"
#include "../common_microcode.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_RegisterBase = BMMQ::_register<AddressType>;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;

template<>
struct BMMQ::CPU_RegisterPair<uint16_t> :  public BMMQ::_register<uint16_t> {
    union {
        struct {
			uint8_t lo;
			uint8_t hi;
        };
        uint16_t value;
    };
	
	uint16_t operator()(){return value;}
};


class LR3592_DMG : public BMMQ::CPU<AddressType, DataType> {
	BMMQ::Imicrocode microcodeList;
	BMMQ::OpcodeList<DataType> opcodeList;
	BMMQ::MemoryPool<AddressType, DataType> mem;
	LR3592_Register mar;
	LR3592_RegisterPair mdr;
	LR3592_RegisterPair md2;
	DataType cip;
public:
	LR3592_Register SP, PC;
	LR3592_RegisterPair AF, BC, DE, HL;
	
    BMMQ::fetchBlock<AddressType, DataType> fetch()
    {
        // building a static fetchblock for testing
        BMMQ::fetchBlock<AddressType, DataType> f ;
        f.baseAddress = 0;

        BMMQ::fetchBlockData<AddressType, DataType> data {0, std::vector<DataType> {0} };
        return f;
    };

    BMMQ::executionBlock decode(BMMQ::OpcodeList<DataType> &oplist, const BMMQ::fetchBlock<AddressType, DataType>& fetchData)
    {
        // building a static execution block
        BMMQ::executionBlock b;
        BMMQ::IOpcode code = oplist.at(0);
        b.push_back(code);
        return b;
    };

    void execute(const BMMQ::executionBlock& block)
    {
        for (auto e : block)
            (e)();
    };
	
	bool checkJumpCond(DataType opcode){
		// Zero Flag is AF.lo & 0x80
		// Carry Flag is AF.lo & 0x10
		bool checkFlag = (opcode & 0x10) == 0x10 ? ((AF.lo & 0x80) >> 7) : ((AF.lo & 0x10) >> 4);
		bool checkSet = opcode & 0x8;
		return !(checkFlag ^ checkSet);
	}
	
	AddressType* ld_R16_I16_GetRegister(DataType opcode){
		uint8_t regInd = (opcode & 0x30) >> 4;
		
		switch (regInd){
			case 0:
				return &BC.value;
			case 1:
				return &DE.value;
			case 2:
				return &HL.value;
			case 3:
				return &SP.value;
			default:
				throw new std::invalid_argument("error in decoding register. invalid argument");
		}
	}
	
	AddressType* add_HL_r16_GetRegister(DataType opcode){
		return ld_R16_I16_GetRegister(opcode);
	}
	
	AddressType* ld_r16_8_GetRegister(DataType opcode) {
		switch (opcode & 0x30 >> 4) {
			case 0:
				return &BC.value;
			case 1:
				return &DE.value;
			case 2:
			case 3:
				return &HL.value;
			default:
				throw new std::invalid_argument("error in decoding register. invalid argument");
		}
	}
	
 	// LD ir16/8 <-> A
	std::pair<DataType*, DataType*>  ld_r16_8_GetOperands(DataType opcode){
		AddressType *reg16 = ld_R16_I16_GetRegister(opcode);
		DataType *accumulator = &AF.hi;
		
		DataType *temp = mem.getPos((std::size_t)(*reg16));
		
		DataType srcFlag = (opcode & 8) >> 3;
		switch(srcFlag) {
			case 0: // Accumulator source
				return std::make_pair(temp, accumulator);
			case 1: // Accumulator Destination
				return std::make_pair(accumulator, temp);
		}
	}
	
	DataType* ld_r8_i8_GetRegister(DataType opcode){
		auto regSet = (opcode & 0x38) >> 3;
		
		switch (regSet) {
			case 0:
				return &BC.hi;
			case 1:
				return &BC.lo;
			case 2:
				return &DE.hi;
			case 3:
				return &DE.lo;
			case 4:
				return &HL.hi;
			case 5:
				return &HL.lo;
			case 6:
				return mem.getPos((std::size_t)HL.value);
			case 7:
				return &AF.hi;			
		}
	}
	
	void rotateAccumulator(DataType opcode){
		
		bool carryFlag = (AF.lo & 0x10);
		DataType lrSet = (opcode & 0x18) >> 3;
			
		switch (lrSet) {
			case 0:
				BMMQ::CML::rlc8(&AF.hi);
				break;
			case 1:
				BMMQ::CML::rrc8(&AF.hi);
				break;
			case 2:
				BMMQ::CML::rl8(&AF.hi, carryFlag);
				break;
			case 3:
				BMMQ::CML::rr8(&AF.hi, carryFlag);
				break;
		}			
	}		
	
	void manipulateAccumulator(DataType opcode){
		DataType opSet = (opcode & 8) >> 3;
		
		switch (opSet) {
			case 0:
				BMMQ::CML::daa(&AF.hi);
				break;
			case 1:
				BMMQ::CML::cpl(&AF.hi);
				break;
			default:
				throw new std::invalid_argument("error in decoding register. invalid argument");
		}
	}
	
	// Incomplete. need to build the mask
	void manipulateCarry(DataType opcode) {
		DataType opSet =  (opcode & 0x8) >> 3;
		
		DataType flags = AF.lo;
		
		switch (opSet) {
			case 0:
				flags |= (1 << 4);
				break;
			case 1:
				flags &= ~(1 << 4);
				break;
			default:
				throw new std::invalid_argument("error in decoding register. invalid argument");
		}
		
		BMMQ::CML::setFlags(&AF.lo, flags);
	}		

	void populateOpcodes() {
		microcodeList.registerMicrocode("jr_i8", [this]() {BMMQ::CML::jr( (&this->PC.value), this->mdr.value, true ); } );
		microcodeList.registerMicrocode("jrcc_i8", [this]() {BMMQ::CML::jr( (&this->PC.value), this->mdr.value, checkJumpCond(cip) ); } );
		microcodeList.registerMicrocode("ld_ir16_SP", [this](){BMMQ::CML::loadtmp( (&this->mar.value), this->SP.value );});
		microcodeList.registerMicrocode("ld_r16_i16", [this](){BMMQ::CML::loadtmp( ld_R16_I16_GetRegister(cip), this->mdr.value );});
		microcodeList.registerMicrocode("add_HL_r16", [this](){auto operands = ld_r16_8_GetOperands(cip);BMMQ::CML::add( operands.first, operands.second );});
		microcodeList.registerMicrocode("inc16", [this](){BMMQ::CML::inc(ld_R16_I16_GetRegister(cip));});
		microcodeList.registerMicrocode("dec16", [this](){BMMQ::CML::dec(ld_R16_I16_GetRegister(cip));});
		microcodeList.registerMicrocode("ld_r8_i8", [this](){BMMQ::CML::loadtmp( ld_r8_i8_GetRegister(cip), mdr.value); });
		microcodeList.registerMicrocode("rotateAccumulator", [this](){rotateAccumulator(cip);});
		microcodeList.registerMicrocode("manipulateAccumulator", [this](){manipulateAccumulator(cip);});
		microcodeList.registerMicrocode("manipulateCarry", [this](){ manipulateCarry(cip);});
	}
};

#endif //DMG_CPU