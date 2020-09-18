#ifndef DMG_CPU
#define DMG_CPU

#include <stdexcept>
#include <cstdio>

#include "../opcode.hpp"
#include "../inst_cycle.hpp"
#include "../cpu.hpp"
#include "../common_microcode.hpp"
#include "../templ/reg_uint16.hpp"
#include "../memory_pool.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;

class LR3592_DMG : public BMMQ::CPU<AddressType, DataType> {
    BMMQ::Imicrocode<AddressType, DataType> microcodeList;
    BMMQ::OpcodeList<AddressType, DataType> opcodeList;
    BMMQ::MemoryPool<AddressType, DataType> mem;
    LR3592_Register mar;
    LR3592_RegisterPair mdr;
    uint16_t flagset;
    DataType cip;
    bool ime = false, stopFlag = false;
public:
    BMMQ::RegisterFile<AddressType> file;
    BMMQ::RegisterInfo<AddressType> AF, BC, DE, HL, SP, PC;

    //CTOR

    LR3592_DMG()
    {
        file = buildRegisterfile();

        AF.registration(file, "AF");
        BC.registration(file, "BC");
        DE.registration(file, "DE");
        HL.registration(file, "HL");
        SP.registration(file, "SP");
        PC.registration(file, "PC");

        populateOpcodes();
    }

	BMMQ::RegisterFile<AddressType> buildRegisterfile(){
		
		BMMQ::RegisterFile<AddressType> regfile;
		
		regfile.addRegister("AF", true);
        regfile.addRegister("BC", true);
        regfile.addRegister("DE", true);
        regfile.addRegister("HL", true);
        regfile.addRegister("SP", false);
        regfile.addRegister("PC", false);
        //regfile.addRegister // mar
        //regfile.addRegister // mdr
		
		return regfile;
	}
    //
    BMMQ::fetchBlock<AddressType, DataType> fetch()
    {
        // building a static fetchblock for testing
        BMMQ::fetchBlock<AddressType, DataType> f ;
        f.setbaseAddress(0);

        BMMQ::fetchBlockData<AddressType, DataType> data {0, std::vector<DataType> {0x3E} };

        f.getblockData().push_back(data);
        return f;
    };

    BMMQ::executionBlock<AddressType, DataType> decode(BMMQ::OpcodeList<AddressType, DataType> &oplist, BMMQ::fetchBlock<AddressType, DataType>& fetchData)
    {
        // building a static execution block
        BMMQ::executionBlock<AddressType, DataType> b;
        mdr.value = 255;
		auto &fb = fetchData.getblockData();
        for( auto& i : fb ) {
            for (auto data : i.data)
                b.push_back(std::make_pair( (fetchData.getbaseAddress() + i.offset ), opcodeList[data]) );
        }
        return b;
    };

    void execute(const BMMQ::executionBlock<AddressType, DataType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb )
    {
        for (auto e : block) {
            (e.second)(fb);
        }
    };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    bool checkJumpCond(DataType opcode)
    {
        auto &A = (LR3592_RegisterPair&)AF;
        bool checkFlag = (opcode & 0x10) == 0x10 ? ((A.lo & 0x80) >> 7) : ((A.lo & 0x10) >> 4);
        bool checkSet = opcode & 0x8;
        return !(checkFlag ^ checkSet);
    }

    AddressType* ld_R16_I16_GetRegister(DataType opcode)
    {

        uint8_t regInd = (opcode & 0x30) >> 4;

        switch (regInd) {
        case 0:
            return &BC->value;
        case 1:
            return &DE->value;
        case 2:
            return &HL->value;
        case 3:
            return &SP->value;
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    AddressType* add_HL_r16_GetRegister(DataType opcode)
    {
        return ld_R16_I16_GetRegister(opcode);
    }

    AddressType* ld_r16_8_GetRegister(DataType opcode)
    {
        switch (opcode & 0x30 >> 4) {
        case 0:
            return &BC->value;
        case 1:
            return &DE->value;
        case 2:
        case 3:
            return &HL->value;
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    // LD ir16/8 <-> A
    std::pair<DataType*, DataType*>  ld_r16_8_GetOperands(DataType opcode)
    {
        AddressType *reg16 = ld_R16_I16_GetRegister(opcode);
        DataType *accumulator = &((LR3592_RegisterPair*)AF())->hi;

        DataType *temp = mem.getPos((std::size_t)(*reg16));

        DataType srcFlag = (opcode & 8) >> 3;
        switch(srcFlag) {
        case 0: // Accumulator source
            return std::make_pair(temp, accumulator);
        case 1: // Accumulator Destination
            return std::make_pair(accumulator, temp);
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    DataType* ld_r8_i8_GetRegister(DataType opcode)
    {
        auto regSet = (opcode & 0x38) >> 3;
        std::cout << regSet << '\n';
        switch (regSet) {
        case 0:
            return &((LR3592_RegisterPair*)BC())->hi;
        case 1:
            return &((LR3592_RegisterPair*)BC())->lo;
        case 2:
            return &((LR3592_RegisterPair*)DE())->hi;
        case 3:
            return &((LR3592_RegisterPair*)DE())->lo;
        case 4:
            return &((LR3592_RegisterPair*)HL())->hi;
        case 5:
            return &((LR3592_RegisterPair*)HL())->lo;
        case 6:
            return mem.getPos((std::size_t)HL->value);
        case 7:
            std::cout << "A\n";
            return &((LR3592_RegisterPair*)AF())->hi;
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    void rotateAccumulator(DataType opcode)
    {

        auto &A = (LR3592_RegisterPair&)AF;

        bool carryFlag = (A.lo & 0x10);
        DataType lrSet = (opcode & 0x18) >> 3;

        switch (lrSet) {
        case 0:
            BMMQ::CML::rlc8(&A.hi);
            break;
        case 1:
            BMMQ::CML::rrc8(&A.hi);
            break;
        case 2:
            BMMQ::CML::rl8(&A.hi, carryFlag);
            break;
        case 3:
            BMMQ::CML::rr8(&A.hi, carryFlag);
            break;
        }
    }

    void manipulateAccumulator(DataType opcode)
    {

        auto &A = ((LR3592_RegisterPair&)AF).hi;
        DataType opSet = (opcode & 8) >> 3;

        switch (opSet) {
        case 0:
            BMMQ::CML::daa(&A);
            break;
        case 1:
            BMMQ::CML::cpl(&A);
            break;
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    // Incomplete. need to build the mask
    void manipulateCarry(DataType opcode)
    {
        DataType opSet =  (opcode & 0x8) >> 3;

        auto &F = ((LR3592_RegisterPair&)AF).lo;
        DataType flags = F;

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

        BMMQ::CML::setFlags(&F, flags);
    }

    DataType* ld_r8_r8_GetRegister(DataType regcode)
    {
        switch (regcode) {
        case 0:
            return &((LR3592_RegisterPair*)BC())->hi;
        case 1:
            return &((LR3592_RegisterPair*)BC())->lo;
        case 2:
            return &((LR3592_RegisterPair*)DE())->hi;
        case 3:
            return &((LR3592_RegisterPair*)DE())->lo;
        case 4:
            return &((LR3592_RegisterPair*)HL())->hi;
        case 5:
            return &((LR3592_RegisterPair*)HL())->lo;
        case 6:
            return mem.getPos((std::size_t)HL->value);
        case 7:
            return &((LR3592_RegisterPair*)AF())->hi;
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    std::pair<DataType*, DataType*> ld_r8_r8_GetOperands(DataType opcode)
    {
        DataType destReg = (opcode & 38) >> 3;
        DataType srcReg = (opcode & 7);

        return std::make_pair(ld_r8_r8_GetRegister(destReg), ld_r8_r8_GetRegister(srcReg) );
    }

    void math_r8(DataType opcode)
    {
        DataType mathFunc = (opcode & 38) >> 3;
        DataType* srcReg = ld_r8_r8_GetRegister(opcode & 7);

        auto &A = (LR3592_RegisterPair&)AF;
        bool carryFlag = (A.lo & 0x10);

        switch (mathFunc) {
        case 0:
            BMMQ::CML::add(&A.hi, srcReg);
            break;
        case 1:
            BMMQ::CML::adc(&A.hi, srcReg, carryFlag);
            break;
        case 2:
            BMMQ::CML::sub(&A.hi, srcReg);
            break;
        case 3:
            BMMQ::CML::sbc(&A.hi, srcReg, carryFlag);
            break;
        case 4:
            BMMQ::CML::iand(&A.hi, srcReg);
            break;
        case 5:
            BMMQ::CML::ixor(&A.hi, srcReg);
            break;
        case 6:
            BMMQ::CML::ior(&A.hi, srcReg);
            break;
        case 7:
            BMMQ::CML::cmp(&A.hi, srcReg);
            break;
        }
    }

    void math_i8(DataType opcode)
    {
        DataType mathFunc = (opcode & 38) >> 3;

        auto &A = (LR3592_RegisterPair&)AF;
        bool carryFlag = (A.lo & 0x10);

        switch (mathFunc) {
        case 0:
            BMMQ::CML::add(&A.hi, mdr.value);
            break;
        case 1:
            BMMQ::CML::adc(&A.hi, mdr.value, carryFlag);
            break;
        case 2:
            BMMQ::CML::sub(&A.hi, mdr.value);
            break;
        case 3:
            BMMQ::CML::sbc(&A.hi, mdr.value, carryFlag);
            break;
        case 4:
            BMMQ::CML::iand(&A.hi, mdr.value);
            break;
        case 5:
            BMMQ::CML::ixor(&A.hi, mdr.value);
            break;
        case 6:
            BMMQ::CML::ior(&A.hi, mdr.value);
            break;
        case 7:
            BMMQ::CML::cmp(&A.hi, mdr.value);
            break;
        }
    }

    void ret()
    {
        PC->value = mem.read(SP->value);
        SP->value += 2;
    }

    void ret_cc(DataType opcode)
    {
        if ( checkJumpCond(opcode))
            ret();
    }

    AddressType *push_pop_GetRegister(DataType opcode)
    {
        DataType regCode = (opcode & 0x10) >> 4;

        switch (regCode) {
        case 0:
            return &BC->value;
        case 1:
            return &DE->value;
        case 2:
            return &HL->value;
        case 3:
            return &AF->value;
        default:
            throw new std::invalid_argument("error in decoding register. invalid argument");
        }
    }

    void pop(DataType opcode)
    {
        AddressType *reg = push_pop_GetRegister(opcode);
        *reg = mem.read(SP->value);
        SP->value += 2;
    }

    void push(DataType opcode)
    {
        AddressType *reg = push_pop_GetRegister(opcode);
        *mem.getPos(SP->value) = *reg;
        SP->value += 2;
    }

    void call()
    {
        *mem.getPos(SP->value) = PC->value;
        BMMQ::CML::jr( &PC->value, mdr.value, true );
        SP->value -=2;
    }

    void call_cc(DataType opcode)
    {
        if( checkJumpCond(opcode)) {
            call();
        }
    }

    void rst(DataType opcode)
    {
        DataType rstPos = (opcode & 0x38);
        *mem.getPos(SP->value) = PC->value;
        BMMQ::CML::jr( &PC->value, rstPos, true );
    }

    void ldh(DataType opcode)
    {
        DataType* dest;
        DataType* src;

        auto srcSet = (opcode & 0x10) >> 4; // checks if Accumulator is the source

        auto &A = (LR3592_RegisterPair&)AF;
        if (srcSet) {
            src = &A.hi;
            dest = mem.getPos(mdr.value + 0xFF00);
        }
        else {
            src = &A.hi;
            dest = mem.getPos(mdr.value + 0xFF00);
        }

        BMMQ::CML::loadtmp(dest, src);
    }

    void ld_ir16_r8(DataType opcode)
    {
        DataType* dest;
        DataType* src;

        auto regSet = (opcode & 8) >> 3;
        auto srcSet = (opcode & 0x10) >> 4;

        auto &A = (LR3592_RegisterPair&)AF;
        auto &C = (LR3592_RegisterPair&)BC;
        switch (srcSet) {
        case 0:
            src = &A.hi;
            dest = (regSet) ? mem.getPos( C.lo + 0xFF00 ) : mem.getPos(mdr.value);
            break;
        case 1:
            src = (regSet) ? mem.getPos(C.lo + 0xFF00) : mem.getPos(mdr.value);
            dest = &A.hi;
        }

        BMMQ::CML::loadtmp(dest, src);
    }

    void ei_di(DataType opcode)
    {
        ime = (opcode & 8 ) >> 3;
    }

    void ld_hl_sp(DataType opcode)
    {
        AddressType *dest;
        AddressType *src;

        DataType srcSet = (opcode & 1);

        switch (srcSet) {
        case 0:
            dest = &HL->value;
            src = &SP->value;
            *src += mdr.lo;
        case 1:
            dest = &SP->value;
            src = &HL->value;
        }

        BMMQ::CML::loadtmp(dest, src);
    }

    void cb_execute(DataType opcode)
    {
        auto operation = ( opcode & 0xF8 ) >> 3;
        DataType testBit = (opcode & 38) >> 3;
        auto reg = ( opcode & 7 );
        auto dest = ld_r8_r8_GetRegister(reg);

        auto quadrant = opcode >> 6;

        DataType* tempFlag;
        auto &F = ((LR3592_RegisterPair&)AF).lo;
        switch(quadrant) {
        case 1:
            BMMQ::CML::testbit8(tempFlag, dest, testBit);
            break;
        case 2:
            BMMQ::CML::resetbit8(dest, testBit);
            break;
        case 3:
            BMMQ::CML::setbit8(dest, testBit);
            break;
        case 0:
            switch (operation) {
            case 0:
                BMMQ::CML::rlc8(dest);
                break;
            case 1:
                BMMQ::CML::rrc8(dest);
                break;
            case 2:
                BMMQ::CML::rl8(dest, (F & 0x10) );
                break;
            case 3:
                BMMQ::CML::rr8(dest, (F & 0x10) );
                break;
            case 4:
                BMMQ::CML::sla8(dest);
                break;
            case 5:
                BMMQ::CML::sra8(dest);
                break;
            case 6:
                BMMQ::CML::swap8(dest);
                break;
            case 7:
                BMMQ::CML::srl8(dest);
                break;
            }
            break;
        }
    }

    //	calculation flags:
    //	each flag has two bytes, read in the endianness of the system
    //	so, for a system with 4 flag bits, the flags hold 8 bits.
    //	Values:
    //  00: No check, 01: Check Flag, 10: reset flag, 11: set flag
    // 	the second bit takes precidence
    void calculateflags(uint16_t calculationFlags)
    {
        bool newflags[4] = {0,0,0,0};

        auto &A = ((LR3592_RegisterPair&)AF).hi;
        auto &F = ((LR3592_RegisterPair&)AF).lo;
        // Zero flag
        switch(calculationFlags & 0xc0) {
        case 0xC0:
            newflags[0] = 1;
            break;
        case 0x80:
            newflags[0] = 0;
            break;
        case 0x40:
            newflags[0] = (&A == 0);
            break;
        }

        // Negative flag
        switch(calculationFlags & 0x30) {
        case 0x30:
            newflags[1] = 1;
            break;
        case 0x20:
            newflags[1] = 0;
            break;
            //	case 0x10:
            //	TODO: Any Negative checks
        }

        // Half-Carry Flag
        switch(calculationFlags & 0xC) {
        case 0xC:
            newflags[2] = 1;
            break;
        case 0x8:
            newflags[2] = 0;
            break;
        case 0x4:
            newflags[2] = ( ( (mdr.value & 0xF) + (A & 0xF) ) & 0x10 ) == 0x10;
        }

        // Carry Flag
        switch(calculationFlags & 3) {
        case 3:
            newflags[3] = 1;
        case 2:
            newflags[3] = 0;
        case 1:
            newflags[3] = (A - mdr.value) > (A - 255);
        }

        F = (newflags[0] << 7) | (newflags[1] << 6) | (newflags[2] << 5) | (newflags[3] << 4);
    }

    void nop()  {}
    void stop()
    {
        stopFlag = true;
    }

    void populateOpcodes()
    {
        microcodeList.registerMicrocode("jp_i16", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::jp( (&this->PC->value), this->mdr.value, true );
        } );
        microcodeList.registerMicrocode("jpcc_i16", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::jp( (&this->PC->value), this->mdr.value, checkJumpCond(cip) );
        } );
        microcodeList.registerMicrocode("jr_i8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::jr( (&this->PC->value), this->mdr.value, true );
        } );
        microcodeList.registerMicrocode("jrcc_i8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::jr( (&this->PC->value), this->mdr.value, checkJumpCond(cip) );
        } );
        microcodeList.registerMicrocode("ld_ir16_SP", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::loadtmp( (&this->mar.value), this->SP->value );
        });
        microcodeList.registerMicrocode("ld_r16_i16", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::loadtmp( ld_R16_I16_GetRegister(cip), this->mdr.value );
        });
        microcodeList.registerMicrocode("ld_r16_8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            auto operands = ld_r16_8_GetOperands(cip);
            BMMQ::CML::loadtmp( operands.first, operands.second );
        });
        microcodeList.registerMicrocode("add_HL_r16", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            auto operands = ld_r16_8_GetOperands(cip);
            BMMQ::CML::add( operands.first, operands.second );
        });
        microcodeList.registerMicrocode("inc16", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::inc(ld_R16_I16_GetRegister(cip));
        });
        microcodeList.registerMicrocode("dec16", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::dec(ld_R16_I16_GetRegister(cip));
        });
        microcodeList.registerMicrocode("inc8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::inc(ld_r8_i8_GetRegister(cip));
        });
        microcodeList.registerMicrocode("dec8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::dec(ld_r8_i8_GetRegister(cip));
        });
        microcodeList.registerMicrocode("ld_r8_i8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::loadtmp( ld_r8_i8_GetRegister(cip), mdr.value);
        });
        microcodeList.registerMicrocode("rotateAccumulator", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            rotateAccumulator(cip);
        });
        microcodeList.registerMicrocode("manipulateAccumulator", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            manipulateAccumulator(cip);
        });
        microcodeList.registerMicrocode("manipulateCarry", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            manipulateCarry(cip);
        });
        microcodeList.registerMicrocode("ld_r8_r8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            auto operands = ld_r8_r8_GetOperands(cip);
            BMMQ::CML::loadtmp( operands.first, operands.second );
        });
        microcodeList.registerMicrocode("math_r8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            math_r8(cip);
        });
        microcodeList.registerMicrocode("math_i8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            math_i8(cip);
        });
        microcodeList.registerMicrocode("ret_cc", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            ret_cc(cip);
        });
        microcodeList.registerMicrocode("ret", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            ret();
        });
        microcodeList.registerMicrocode("pop", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            pop(cip);
        } );
        microcodeList.registerMicrocode("push", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            push(cip);
        } );
        microcodeList.registerMicrocode("call", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            call();
        });
        microcodeList.registerMicrocode("call_cc", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            call_cc(cip);
        });
        microcodeList.registerMicrocode("rst", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            rst(cip);
        });
        microcodeList.registerMicrocode("ldh", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            ldh(cip);
        });
        microcodeList.registerMicrocode("ld_ir16_r8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            ld_ir16_r8(cip);
        });
        microcodeList.registerMicrocode("add_sp_r8", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::add(&SP->value, mdr.value);
        });
        microcodeList.registerMicrocode("jp_hl", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            BMMQ::CML::loadtmp(&PC->value, mem.getPos(HL->value) );
        });
        microcodeList.registerMicrocode("ei_di", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            ei_di(cip);
        });
        microcodeList.registerMicrocode("ld_hl_sp", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            ld_hl_sp(cip);
        });
        microcodeList.registerMicrocode("cb", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            cb_execute(mdr.value);
        } );
        microcodeList.registerMicrocode("nop", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            nop();
        });
        microcodeList.registerMicrocode("stop", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            stop();
        });
        microcodeList.registerMicrocode("manipulateFlags", [this](BMMQ::fetchBlock<AddressType, DataType> fb) {
            calculateflags(flagset);
        });


        BMMQ::IOpcode NOP {microcodeList.findMicrocode("nop")};																				// 00h
        BMMQ::IOpcode LD_R16_I16{microcodeList.findMicrocode("ld_r16_i16")};																// 01h, 11h, 21h, 31h
        BMMQ::IOpcode LD_R16_8{microcodeList.findMicrocode("ld_r16_8")};																	// 02h, 0Ah, 12h, 1Ah, 22h, 2Ah, 32h, 3Ah
        BMMQ::IOpcode INC16 {microcodeList.findMicrocode("inc16") };																		// 03h, 13h, 23h, 33h
        BMMQ::IOpcode INC8 {microcodeList.findMicrocode("inc8"), microcodeList.findMicrocode("manipulateFlags")};							// 04h, 0Ch, 14h, 1Ch, 24h, 2Ch, 34h, 3Ch
        BMMQ::IOpcode DEC8 {microcodeList.findMicrocode("dec8"), microcodeList.findMicrocode("manipulateFlags")};							// 05h, 0Dh, 15h, 1Dh, 25h, 2Dh, 35h, 3Dh
        BMMQ::IOpcode LD_R8_I8 {microcodeList.findMicrocode("ld_r8_i8")};																	// 06h, 0Eh, 16h, 1Eh, 26h, 2Eh, 36h, 3Eh
        BMMQ::IOpcode ROTATE_A {microcodeList.findMicrocode("rotateAccumulator"), microcodeList.findMicrocode("manipulateFlags")};			// 07h, 0Fh, 17h, 1Fh
        BMMQ::IOpcode LD_IR16_SP {microcodeList.findMicrocode("ld_ir16_SP")};																// 08h
        BMMQ::IOpcode ADD_HL_R16 {microcodeList.findMicrocode("add_HL_r16"), microcodeList.findMicrocode("manipulateFlags")};				// 09h, 19h, 29h, 39h
        BMMQ::IOpcode DEC16 {microcodeList.findMicrocode("dec16")};																			// 0Bh, 1Bh, 2Bh, 3Bh
        BMMQ::IOpcode STOP {microcodeList.findMicrocode("stop")};
        BMMQ::IOpcode JR_I8 {microcodeList.findMicrocode("jr_i8")};																			// 18h
        BMMQ::IOpcode JR_CC_8 {microcodeList.findMicrocode("jrcc_i8")};																		// 20h, 28h, 30h, 38h
        BMMQ::IOpcode MANIPULATE_A {microcodeList.findMicrocode("manipulateAccumulator"), microcodeList.findMicrocode("manipulateFlags")};	// 27h, 3Fh
        BMMQ::IOpcode MANIPULATE_CF {microcodeList.findMicrocode("manipulateCarry")};														// 37h, 3Fh
        BMMQ::IOpcode LD_R8_R8 {microcodeList.findMicrocode("ld_r8_r8")};																	// 40h - 7Fh
        BMMQ::IOpcode MATH_R8 {microcodeList.findMicrocode("math_r8"), microcodeList.findMicrocode("manipulateFlags")}; 					// 80h - BFh
        BMMQ::IOpcode RET_CC {microcodeList.findMicrocode("ret_cc")};																		// C0h, C8h, D0h, D8h
        BMMQ::IOpcode POP {microcodeList.findMicrocode("pop")};																				// C1h, D1h, E1h, F1h
        BMMQ::IOpcode JPCC_I16 {microcodeList.findMicrocode("jp_i16")};																		// C2h, CAh, D2h, DAh
        BMMQ::IOpcode JP_I16 {microcodeList.findMicrocode("jpcc_i16")};																		// C3h
        BMMQ::IOpcode CALLCC_I16 {microcodeList.findMicrocode("call_cc")};																	// C4h, CCh, D4h, DCh
        BMMQ::IOpcode PUSH {microcodeList.findMicrocode("push")};																			// C5h, D5h, E5h, F5h
        BMMQ::IOpcode MATH_I8 {microcodeList.findMicrocode("math_i8"), microcodeList.findMicrocode("manipulateFlags")};						// C6h, CEh, D6h, DEh, E6h, EEh, F6h, FFh
        BMMQ::IOpcode RST {microcodeList.findMicrocode("rst")};																				// C7h, CFh, D7h, DFh, E7h, EFh, F7h, FFh
        BMMQ::IOpcode RET {microcodeList.findMicrocode("ret")};																				// C9h, D9h
        BMMQ::IOpcode CB {microcodeList.findMicrocode("cb")};																				// CBh
        BMMQ::IOpcode CALL {microcodeList.findMicrocode("call")};																			// CDh
        BMMQ::IOpcode LDH {microcodeList.findMicrocode("ldh")};																				// E0h, F0h
        BMMQ::IOpcode LD_IR16_R8 {microcodeList.findMicrocode("ld_ir16_r8")};																// E2h, EAh, F2h, FAh
        BMMQ::IOpcode EI_DI {microcodeList.findMicrocode("ei_di")};																			// F3h, FBh
        BMMQ::IOpcode ADD_SP_R8 {microcodeList.findMicrocode("add_sp_r8")};																	// E8h
        BMMQ::IOpcode JP_HL {microcodeList.findMicrocode("jp_hl")};																			// E9h
        BMMQ::IOpcode LD_HL_SP {microcodeList.findMicrocode("ld_hl_sp")};																	// F8h
        opcodeList.assign({
            {NOP},      {LD_R16_I16}, {LD_R16_8},   {INC16},    {INC8},       {DEC8},     {LD_R8_I8}, {ROTATE_A},      {LD_IR16_SP}, {ADD_HL_R16}, {LD_R16_8},   {DEC16},    {INC8},       {DEC8},     {LD_R8_I8}, {ROTATE_A},
            {STOP},     {LD_R16_I16}, {LD_R16_8},   {INC16},    {INC8},       {DEC8},     {LD_R8_I8}, {ROTATE_A},      {JR_I8},      {ADD_HL_R16}, {LD_R16_8},   {DEC16},    {INC8},       {DEC8},     {LD_R8_I8}, {ROTATE_A},
            {JR_CC_8},  {LD_R16_I16}, {LD_R16_8},   {INC16},    {INC8},       {DEC8},     {LD_R8_I8}, {MANIPULATE_A},  {JR_CC_8},    {ADD_HL_R16}, {LD_R16_8},   {DEC16},    {INC8},       {DEC8},     {LD_R8_I8}, {MANIPULATE_A},
            {JR_CC_8},  {LD_R16_I16}, {LD_R16_8},   {INC16},    {INC8},       {DEC8},     {LD_R8_I8}, {MANIPULATE_CF}, {JR_CC_8},    {ADD_HL_R16}, {LD_R16_8},   {DEC16},    {INC8},       {DEC8},     {LD_R8_I8}, {MANIPULATE_CF},
            {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},      {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},
            {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},      {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},
            {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},      {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},
            {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},      {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8},   {LD_R8_R8}, {LD_R8_R8}, {LD_R8_R8},
            {MATH_R8},  {MATH_R8},	  {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},       {MATH_R8},    {MATH_R8},    {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},
            {MATH_R8},  {MATH_R8},	  {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},       {MATH_R8},    {MATH_R8},    {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},
            {MATH_R8},  {MATH_R8},	  {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},       {MATH_R8},    {MATH_R8},    {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},
            {MATH_R8},  {MATH_R8},	  {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},       {MATH_R8},    {MATH_R8},    {MATH_R8},    {MATH_R8},  {MATH_R8},    {MATH_R8},  {MATH_R8},  {MATH_R8},
            {RET_CC},   {POP},        {JPCC_I16},   {JP_I16},   {CALLCC_I16}, {PUSH},     {MATH_I8},  {RST},           {RET_CC},     {RET},        {JPCC_I16},   {CB},       {CALLCC_I16}, {CALL},     {MATH_I8},  {RST},
            {RET_CC},   {POP},        {JPCC_I16},   {NOP},      {CALLCC_I16}, {PUSH},     {MATH_I8},  {RST},           {RET_CC},     {RET},        {JPCC_I16},   {NOP},      {CALLCC_I16}, {NOP},      {MATH_I8},  {RST},
            {LDH},      {POP},        {LD_IR16_R8}, {NOP},      {NOP},        {PUSH},     {MATH_I8},  {RST},           {ADD_SP_R8},  {JP_HL},      {LD_IR16_R8}, {NOP},      {NOP},        {NOP},      {MATH_I8},  {RST},
            {LDH},      {POP},        {LD_IR16_R8}, {EI_DI},    {NOP},        {PUSH},     {MATH_I8},  {RST},           {LD_HL_SP},   {LD_HL_SP},   {LD_IR16_R8}, {EI_DI},    {NOP},        {NOP},      {MATH_I8},  {RST}
        });
    }
};

#endif //DMG_CPU