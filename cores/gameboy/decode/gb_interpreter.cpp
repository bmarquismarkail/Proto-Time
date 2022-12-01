#include "gb_interpreter.hpp"

void LR3592_Interpreter_Decode::setSnapshot(BMMQ::MemorySnapshot<AddressType, DataType, AddressType> *s)
{
	snap = s;
}


LR3592_RegisterPair* LR3592_Interpreter_Decode::GetRegister(std::string_view id)
{
	auto regfile = cpu->getMemory().file;
	snap->copyRegisterFromMainFile(id, regfile);
	return (LR3592_RegisterPair*)snap->file.findRegister(id)->second;
}

bool LR3592_Interpreter_Decode::checkJumpCond(DataType opcode)
{
	auto A = GetRegister("AF");
	bool checkFlag	= (opcode & 0x10) == 0x10 ? ((A->lo & 0x80) >> 7) : ((A->lo & 0x10) >> 4);
	bool checkSet = opcode & 0x8;
	return !(checkFlag ^ checkSet);
}

AddressType* LR3592_Interpreter_Decode::ld_R16_I16_GetRegister(DataType opcode)
{


	uint8_t regInd = (opcode & 0x30) >> 4;

	switch (regInd) {
	case 0:
		return &(GetRegister("BC")->value);
	case 1:
		return &(GetRegister("DE")->value);
	case 2:
		return &(GetRegister("HL")->value);
	case 3:
		return &(GetRegister("SP")->value);
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

AddressType* LR3592_Interpreter_Decode::add_HL_r16_GetRegister(DataType opcode)
{
	return ld_R16_I16_GetRegister(opcode);
}

AddressType* LR3592_Interpreter_Decode::ld_r16_8_GetRegister(DataType opcode)
{
	switch (opcode & 0x30 >> 4) {
	case 0:
		return &(GetRegister("BC")->value);
	case 1:
		return &(GetRegister("DE")->value);
	case 2:
	case 3:
		return &(GetRegister("HL")->value);
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

// LD ir16/8 <-> A
std::pair<DataType*, DataType*> LR3592_Interpreter_Decode::ld_r16_8_GetOperands(DataType opcode)
{
	AddressType *reg16 = ld_R16_I16_GetRegister(opcode);
	DataType *accumulator = &((LR3592_RegisterPair*)GetRegister("AF"))->hi;
	DataType *temp = new DataType;
	auto memMap = cpu->getMemory();
	snap->mem.read( temp, ((std::size_t)(*reg16)), 1);
	*temp = snap->mem[*reg16];

	DataType srcFlag = (opcode & 8) >> 3;
	switch(srcFlag) {
	case 0: // Accumulator Source
		return std::make_pair(temp, accumulator);
	case 1: // Accumulator Destination
		return std::make_pair(accumulator, temp);
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

DataType* LR3592_Interpreter_Decode::ld_r8_i8_GetRegister(DataType opcode)
{
	auto regSet = (opcode & 0x38) >> 3;
	switch (regSet) {
	case 0:
		return &(GetRegister("BC")->hi);
	case 1:
		return &((LR3592_RegisterPair*)GetRegister("BC"))->lo;
	case 2:
		return &((LR3592_RegisterPair*)GetRegister("DE"))->hi;
	case 3:
		return &((LR3592_RegisterPair*)GetRegister("DE"))->lo;
	case 4:
		return &((LR3592_RegisterPair*)GetRegister("HL"))->hi;
	case 5:
		return &((LR3592_RegisterPair*)GetRegister("HL"))->lo;
	case 6:
		{
			auto HL = GetRegister("HL");
			DataType *temp = new DataType;
			auto memMap = cpu->getMemory();
			snap->mem.read( temp, ((std::size_t)HL->value), 1);
			return temp;
		}
	case 7:
		return &(GetRegister("AF")->hi);
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

void LR3592_Interpreter_Decode::rotateAccumulator(DataType opcode)
{

	auto A =GetRegister("AF");

	bool carryFlag = (A->lo & 0x10);
	DataType lrSet = (opcode & 0x18) >> 3;

	switch (lrSet) {
	case 0:
		BMMQ::CML::rlc8(&A->hi);
		break;
	case 1:
		BMMQ::CML::rrc8(&A->hi);
		break;
	case 2:
		BMMQ::CML::rl8(&A->hi, carryFlag);
		break;
	case 3:
		BMMQ::CML::rr8(&A->hi, carryFlag);
		break;
	}
}

void LR3592_Interpreter_Decode::manipulateAccumulator(DataType opcode)
{

	auto A =GetRegister("AF");
	DataType opSet = (opcode & 8) >> 3;

	switch (opSet) {
	case 0:
		BMMQ::CML::daa(A);
		break;
	case 1:
		BMMQ::CML::cpl(A);
		break;
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

// Incomplete. need	to build the mask
void LR3592_Interpreter_Decode::manipulateCarry(DataType opcode)
{
	DataType opSet =  (opcode & 0x8) >> 3;

	auto *F = &(GetRegister("AF")->lo);
	DataType flags = *F;

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

	BMMQ::CML::setFlags(F, flags);
}

DataType* LR3592_Interpreter_Decode::ld_r8_r8_GetRegister(DataType regcode)
{
	switch (regcode) {
	case 0:
		return &(GetRegister("BC")->hi);
	case 1:
		return &(GetRegister("BC")->lo);
	case 2:
		return &(GetRegister("DE")->hi);
	case 3:
		return &(GetRegister("DE")->lo);
	case 4:
		return &(GetRegister("HL")->hi);
	case 5:
		return &(GetRegister("HL")->lo);
	case 6:
		{
			auto HL = GetRegister("HL");
			DataType *temp = new DataType;
			auto memMap = cpu->getMemory();
			snap->mem.read( temp, ((std::size_t)HL->value), 1);
			return temp;
		}
	case 7:
		return &(GetRegister("AF")->hi);
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

std::pair<DataType*, DataType*> LR3592_Interpreter_Decode::ld_r8_r8_GetOperands(DataType opcode)
{
	DataType destReg = (opcode & 38) >> 3;
	DataType srcReg = (opcode & 7);

	return std::make_pair(ld_r8_r8_GetRegister(destReg), ld_r8_r8_GetRegister(srcReg) );
}

void LR3592_Interpreter_Decode::math_r8(DataType opcode)
{
	DataType mathFunc = (opcode & 38) >> 3;
	DataType* srcReg = ld_r8_r8_GetRegister(opcode & 7);

	auto A = GetRegister("AF");
	bool carryFlag = (A->lo & 0x10);

	switch (mathFunc) {
	case 0:
		BMMQ::CML::add(&A->hi, srcReg);
		break;
	case 1:
		BMMQ::CML::adc(&A->hi, srcReg, carryFlag);
		break;
	case 2:
		BMMQ::CML::sub(&A->hi, srcReg);
		break;
	case 3:
		BMMQ::CML::sbc(&A->hi, srcReg, carryFlag);
		break;
	case 4:
		BMMQ::CML::iand(&A->hi, srcReg);
		break;
	case 5:
		BMMQ::CML::ixor(&A->hi, srcReg);
		break;
	case 6:
		BMMQ::CML::ior(&A->hi, srcReg);
		break;
	case 7:
		BMMQ::CML::cmp(&A->hi, srcReg);
		break;
	}
}


//TODO: MDR needs to be converted to a value in the MemorySnapshot
void LR3592_Interpreter_Decode::math_i8(DataType opcode)
{
	DataType mdr= 0;
	DataType mathFunc = (opcode & 38) >> 3;

	auto A = GetRegister("AF");
	bool carryFlag = (A->lo & 0x10);

	switch (mathFunc) {
	case 0:
		BMMQ::CML::add(&A->hi, mdr);
		break;
	case 1:
		BMMQ::CML::adc(&A->hi, mdr, carryFlag);
		break;
	case 2:
		BMMQ::CML::sub(&A->hi, mdr);
		break;
	case 3:
		BMMQ::CML::sbc(&A->hi, mdr, carryFlag);
		break;
	case 4:
		BMMQ::CML::iand(&A->hi, mdr);
		break;
	case 5:
		BMMQ::CML::ixor(&A->hi, mdr);
		break;
	case 6:
		BMMQ::CML::ior(&A->hi, mdr);
		break;
	case 7:
		BMMQ::CML::cmp(&A->hi, mdr);
		break;
	}
}

//TODO: Brainstorm on making read more templatic
//TODO: third argument in read needs to be dynamic
void LR3592_Interpreter_Decode::ret()
{
	auto PC = GetRegister("PC");
	auto SP = GetRegister("SP");
	auto memMap = cpu->getMemory();
	snap->mem.read( (DataType*)&PC->value, ((std::size_t)SP->value), 1);
	SP->value += 2;
}

void LR3592_Interpreter_Decode::ret_cc(DataType opcode)
{
	if ( checkJumpCond(opcode))
		ret();
}

AddressType* LR3592_Interpreter_Decode::push_pop_GetRegister(DataType opcode)
{
	DataType regCode =	(opcode	& 0x10)	>> 4;

	switch (regCode) {
	case 0:
		return &(GetRegister("BC")->value);
	case 1:
		return &(GetRegister("DE")->value);
	case 2:
		return &(GetRegister("HL")->value);
	case 3:
		return &(GetRegister("AF")->value);
	default:
		throw new std::invalid_argument("error in decoding register. invalid argument");
	}
}

void LR3592_Interpreter_Decode::pop(DataType opcode)
{
	AddressType *reg =	push_pop_GetRegister(opcode);
	auto SP = GetRegister("SP");
	auto memMap = cpu->getMemory();
	snap->mem.read( (DataType*)reg, ((std::size_t)SP->value), 1);
	SP->value += 2;
}

void LR3592_Interpreter_Decode::push(DataType opcode)
{
	AddressType *reg = push_pop_GetRegister(opcode);
	auto SP = GetRegister("SP");
	snap->mem.write((DataType*)reg, SP->value, 2);
	SP->value += 2;
}

void LR3592_Interpreter_Decode::call()
{
	auto SP = GetRegister("SP");
	auto PC = GetRegister("PC");
	auto mdr = GetRegister("mdr");
	snap->mem.write((DataType*)PC, SP->value, 2);
	BMMQ::CML::jr(	&PC->value, mdr->value, true);
	SP->value -=2;
}

void LR3592_Interpreter_Decode::call_cc(DataType opcode)
{
	if( checkJumpCond(opcode) ) {
		call();
	}
}

void LR3592_Interpreter_Decode::rst(DataType opcode)
{
	auto SP = GetRegister("SP");
	auto PC = GetRegister("PC");
	DataType rstPos = (opcode & 0x38);
	snap->mem.write((DataType*)PC, SP->value, 2);
	BMMQ::CML::jr( &PC->value, rstPos, true );
}

void LR3592_Interpreter_Decode::ldh(DataType opcode)
{
	DataType* dest;
	DataType* src;

	auto srcSet = (opcode & 0x10) >> 4;

	auto A = GetRegister("AF");
	auto mdr = GetRegister("mdr");
	if (srcSet) {
		src = &A->hi;
		snap->mem.write(dest, mdr->value + 0xFF00, 1);
	}
	else {
		src = &A->hi;
		snap->mem.write(dest, mdr->value + 0xFF00, 1);
	}

	BMMQ::CML::loadtmp(dest, src);
}

void LR3592_Interpreter_Decode::ld_ir16_r8(DataType opcode)
{
	DataType* dest;
	DataType* src;

	auto regSet = (opcode & 8)	>> 3;
	auto srcSet = (opcode & 0x10) >> 4;

	auto A = GetRegister("AF");
	auto C = GetRegister("BC");
	auto mdr = GetRegister("mdr");
	switch	(srcSet) {
	case 0:
		src = &A->hi;
		snap->mem.write(dest, regSet ? ( C->lo + 0xFF00 ) : mdr->value, 1);
		break;
	case 1:
		snap->mem.write(src, regSet ? ( C->lo + 0xFF00 ) : mdr->value, 1);
		dest = &A->hi;
	}

	BMMQ::CML::loadtmp(dest, src);
}

void LR3592_Interpreter_Decode::ei_di(DataType	opcode)
{
	auto ime = GetRegister("ime");
	ime->value = (opcode & 8 ) >> 3;
}

void LR3592_Interpreter_Decode::ld_hl_sp(DataType opcode)
{
	AddressType *dest;
	AddressType *src;

	DataType srcSet = (opcode & 1);
	auto mdr = GetRegister("mdr");

	switch	(srcSet) {
	case 0:
		dest = &(GetRegister("HL")->value);
		src = &(GetRegister("SP")->value);
		*src += mdr->lo;
		break;
	case 1:
		dest = &(GetRegister("SP")->value);
		src = &(GetRegister("HL")->value);
		break;
	}

	BMMQ::CML::loadtmp(dest, src);
}

void LR3592_Interpreter_Decode::cb_execute(DataType opcode)
{
	auto operation	= ( opcode & 0xF8 ) >> 3;
	DataType testBit = (opcode & 38) >> 3;
	auto reg = ( opcode & 7 );
	auto dest = ld_r8_r8_GetRegister(reg);

	auto quadrant = opcode >> 6;

	DataType tempFlag;
	auto &F = GetRegister("AF")->lo;
	switch(quadrant) {
	case 1:
		BMMQ::CML::testbit8(&tempFlag, dest, testBit);
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
//	so, for a system with 4 flag	bits, the flags	hold 8 bits.
//	Values:
//	00: No check, 01: Check Flag, 10: reset flag, 11: set flag
// 	the	second bit takes precidence
void LR3592_Interpreter_Decode::calculateflags(uint16_t calculationFlags)
{
	bool newflags[4] =	{0,0,0,0};

	auto &A = GetRegister("AF")->hi;
	auto &F = GetRegister("AF")->lo;
	auto mdr = GetRegister("mdr");
	// Zero flag
	switch(calculationFlags & 0xc0) {
	case 0xC0:
		newflags[0] =	1;
		break;
	case 0x80:
		newflags[0] =	0;
		break;
	case 0x40:
		newflags[0] =	(A == 0);
		break;
	}
 
	// Negative flag
	switch(calculationFlags & 0x30) {
	case 0x30:
		newflags[1] =	1;
		break;
	case 0x20:
		newflags[1] =	0;
		break;
		// case 0x10:
		// TODO: Any Negative	checks
	}

	// Half-Carry Flag
	switch(calculationFlags & 0xC) {
	case 0xC:
		newflags[2] =	1;
		break;
	case 0x8:
		newflags[2] =	0;
		break;
	case 0x4:
		newflags[2] = ( ( (mdr->value & 0xF) + (A & 0xF) ) & 0x10 ) == 0x10;
		break;
	}

	//	Carry Flag
	switch(calculationFlags & 3) {
	case 3:
		newflags[3] = 1;
		break;
	case 2:
		newflags[3] = 0;
		break;
	case 1:
		newflags[3] = (A - mdr->value) > (A - 255);
		break;
	}

	F = (newflags[0] << 7) | (newflags[1] << 6) | (newflags[2] << 5) | (newflags[3] << 4);
}

void LR3592_Interpreter_Decode::nop()	{}
void LR3592_Interpreter_Decode::stop()
{
	cpu->setStopFlag(true);
}

void LR3592_Interpreter_Decode::populateOpcodes()
{
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_jp_i16 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file ) {
		setSnapshot(&file);
		BMMQ::CML::jp( &GetRegister("PC")->value, GetRegister("mdr")->value, true );
	};
	
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_jpcc_i16 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		setSnapshot(&file);
		BMMQ::CML::jp( &GetRegister("PC")->value, GetRegister("mdr")->value, checkJumpCond( cip) );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_jr_i8	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		setSnapshot(&file);
		BMMQ::CML::jr( (&GetRegister("PC")->value), GetRegister("mdr")->value, true );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_jrcc_i8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		setSnapshot(&file);
		BMMQ::CML::jr( (&GetRegister("PC")->value), GetRegister("mdr")->value, checkJumpCond( cip) );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_ir16_SP =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		setSnapshot(&file);
		BMMQ::CML::loadtmp( (&GetRegister("mar")->value), &GetRegister("SP")->value);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_r16_i16 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		setSnapshot(&file);
		BMMQ::CML::loadtmp( ld_R16_I16_GetRegister( cip), GetRegister("mdr")->value );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_r16_8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		auto operands = ld_r16_8_GetOperands( cip);
		BMMQ::CML::loadtmp( operands.first, operands.second );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_add_HL_r16 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		auto operands = ld_r16_8_GetOperands( cip );
		BMMQ::CML::add( operands.first, operands.second );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_inc16	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		BMMQ::CML::inc(ld_R16_I16_GetRegister( cip ));
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_dec16	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		BMMQ::CML::dec(ld_R16_I16_GetRegister( cip ));
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_inc8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		BMMQ::CML::inc(ld_r8_i8_GetRegister( cip));
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_dec8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		BMMQ::CML::dec(ld_r8_i8_GetRegister( cip));
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_r8_i8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		BMMQ::CML::loadtmp( ld_r8_i8_GetRegister( cip), mdr.value);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_rotateAccumulator	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		rotateAccumulator( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_manipulateAccumulator	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		manipulateAccumulator( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_manipulateCarry =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		manipulateCarry( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_r8_r8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		auto operands = ld_r8_r8_GetOperands( cip);
		BMMQ::CML::loadtmp( operands.first, operands.second );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_math_r8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		math_r8( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_math_i8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		math_i8( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ret_cc =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		ret_cc( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ret =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		ret();
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_pop =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		pop( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_push =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		push( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_call =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		call();
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_call_cc =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		call_cc( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_rst =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		rst( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ldh =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		ldh( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_ir16_r8 =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		ld_ir16_r8( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_add_sp_r8	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> snapshot) {
		setSnapshot(&file);
		BMMQ::CML::add(&GetRegister("SP")-> value, GetRegister("mdr")->value);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_jp_hl	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		setSnapshot(&file);
		DataType tmp;
		snap->mem.write(&tmp, GetRegister("HL")->value, 1);
		BMMQ::CML::loadtmp(&GetRegister("PC")->value, &tmp );
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ei_di	=
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		ei_di( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_ld_hl_sp =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		ld_hl_sp( cip);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_cb =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		cb_execute( mdr.value);
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_nop =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		nop();
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_stop =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		stop();
	};
	std::function<void(BMMQ::MemorySnapshot<AddressType, DataType, AddressType>)> uf_manipulateFlags =
	[this](BMMQ::MemorySnapshot<AddressType, DataType, AddressType> file) {
		calculateflags( flagset);
	};

	//	Common Microcode
	LR3592_IMicrocode *mc_manipulateFlags = new LR3592_IMicrocode("manipulateFlags", &uf_manipulateFlags);

	//	Opcodes
	LR3592_Opcode NOP {new LR3592_IMicrocode("nop", &uf_nop)};																		// 00h
	LR3592_Opcode LD_R16_I16{new LR3592_IMicrocode("ld_r16_i16", &uf_ld_r16_i16)};													// 01h, 11h, 21h, 31h
	LR3592_Opcode LD_R16_8{new LR3592_IMicrocode("ld_r16_8", &uf_ld_r16_8)};														// 02h, 0Ah, 12h, 1Ah,22h, 2Ah, 32h, 3Ah
	LR3592_Opcode INC16 {new LR3592_IMicrocode("inc16", &uf_inc16)};																// 03h, 13h, 23h, 33h
	LR3592_Opcode INC8 {new LR3592_IMicrocode("inc8", &uf_inc8),  mc_manipulateFlags };												// 04h, 0Ch, 14h, 1Ch, 24h, 2Ch, 34h, 3Ch
	LR3592_Opcode DEC8 {new LR3592_IMicrocode("dec8", &uf_dec8), mc_manipulateFlags };												// 05h, 0Dh, 15h, 1Dh, 25h, 2Dh, 35h, 3Dh
	LR3592_Opcode LD_R8_I8 {new LR3592_IMicrocode("ld_r8_i8", &uf_ld_r8_i8)};														// 06h, 0Eh, 16h, 1Eh, 26h, 2Eh, 36h, 3Eh
	LR3592_Opcode ROTATE_A {new LR3592_IMicrocode("rotateAccumulator",	&uf_rotateAccumulator), mc_manipulateFlags};				// 07h, 0Fh, 17h, 1Fh
	LR3592_Opcode LD_IR16_SP {new LR3592_IMicrocode("ld_ir16_SP", &uf_ld_ir16_SP)};													// 08h
	LR3592_Opcode ADD_HL_R16 {new LR3592_IMicrocode("add_HL_r16", &uf_add_HL_r16), mc_manipulateFlags};								// 09h, 19h, 29h, 39h
	LR3592_Opcode DEC16 {new LR3592_IMicrocode("dec16", &uf_dec16)};																// 0Bh, 1Bh, 2Bh, 3Bh
	LR3592_Opcode STOP {new LR3592_IMicrocode("stop", &uf_stop)};																	// 10h
	LR3592_Opcode JR_I8 {new LR3592_IMicrocode("jr_i8", &uf_jr_i8)};																// 18h
	LR3592_Opcode JR_CC_8 {new LR3592_IMicrocode("jrcc_i8", &uf_jrcc_i8)};															// 20h, 28h, 30h, 38h
	LR3592_Opcode MANIPULATE_A {new LR3592_IMicrocode("manipulateAccumulator",	&uf_manipulateAccumulator), mc_manipulateFlags};	// 27h, 3Fh
	LR3592_Opcode MANIPULATE_CF {new LR3592_IMicrocode("manipulateCarry", &uf_manipulateCarry)};									// 37h, 3Fh
	LR3592_Opcode LD_R8_R8 {new LR3592_IMicrocode("ld_r8_r8", &uf_ld_r8_r8)};														// 40h - 7Fh
	LR3592_Opcode MATH_R8 {new LR3592_IMicrocode("math_r8", &uf_math_r8), mc_manipulateFlags};										// 80h - BFh
	LR3592_Opcode RET_CC {new LR3592_IMicrocode("ret_cc", &uf_ret_cc)};																// C0h, C8h, D0h, D8h
	LR3592_Opcode POP {new LR3592_IMicrocode("pop", &uf_pop)};																		// C1h, D1h, E1h, F1h
	LR3592_Opcode JPCC_I16 {new LR3592_IMicrocode("jpcc_i16", &uf_jpcc_i16)};														// C2h, CAh, D2h, DAh
	LR3592_Opcode JP_I16 {new LR3592_IMicrocode("jp_i16", &uf_jp_i16)};																// C3h
	LR3592_Opcode CALLCC_I16 {new LR3592_IMicrocode("call_cc",	&uf_call_cc)};														// C4h, CCh, D4h, DCh
	LR3592_Opcode PUSH {new LR3592_IMicrocode("push", &uf_push)};																	// C5h, D5h, E5h, F5h
	LR3592_Opcode MATH_I8 {new LR3592_IMicrocode("math_i8", &uf_math_i8), mc_manipulateFlags};										// C6h, CEh, D6h, DEh, E6h, EEh, F6h, FFh
	LR3592_Opcode RST {new LR3592_IMicrocode("rst", &uf_rst)};																		// C7h, CFh, D7h, DFh, E7h, EFh, F7h, FFh
	LR3592_Opcode RET {new LR3592_IMicrocode("ret", &uf_ret)};																		// C9h, D9h
	LR3592_Opcode CB {new LR3592_IMicrocode("cb", &uf_cb)};																			// CBh
	LR3592_Opcode CALL {new LR3592_IMicrocode("call", &uf_call)};																	// CDh
	LR3592_Opcode LDH {new LR3592_IMicrocode("ldh", &uf_ldh)};																		// E0h, F0h
	LR3592_Opcode LD_IR16_R8 {new LR3592_IMicrocode("ld_ir16_r8", &uf_ld_ir16_r8)};													// E2h, EAh, F2h, FAh
	LR3592_Opcode EI_DI {new LR3592_IMicrocode("ei_di", &uf_ei_di)};																// F3h, FBh
	LR3592_Opcode ADD_SP_R8 {new LR3592_IMicrocode("add_sp_r8", &uf_add_sp_r8)};													// E8h
	LR3592_Opcode JP_HL {new LR3592_IMicrocode("jp_hl", &uf_jp_hl)};																// E9h
	LR3592_Opcode LD_HL_SP {new LR3592_IMicrocode("ld_hl_sp", &uf_ld_hl_sp)};														// F8h

	// now populate the list
	opcodeList.assign({
		{NOP},		{LD_R16_I16},	{LD_R16_8},		{INC16},	{INC8},			{DEC8},		{LD_R8_I8},	{ROTATE_A},		{LD_IR16_SP},	{ADD_HL_R16},	{LD_R16_8},		{DEC16},	{INC8},			{DEC8},		{LD_R8_I8},	{ROTATE_A},
		{STOP},		{LD_R16_I16},	{LD_R16_8},		{INC16},	{INC8},			{DEC8},		{LD_R8_I8},	{ROTATE_A},		{JR_I8},		{ADD_HL_R16},	{LD_R16_8},		{DEC16},	{INC8},			{DEC8},		{LD_R8_I8},	{ROTATE_A},
		{JR_CC_8},	{LD_R16_I16},	{LD_R16_8},		{INC16},	{INC8},			{DEC8},		{LD_R8_I8},	{MANIPULATE_A},	{JR_CC_8},		{ADD_HL_R16},	{LD_R16_8},		{DEC16},	{INC8},			{DEC8},		{LD_R8_I8},	{MANIPULATE_A},
		{JR_CC_8},	{LD_R16_I16},	{LD_R16_8},		{INC16},	{INC8},			{DEC8},		{LD_R8_I8},	{MANIPULATE_CF},{JR_CC_8},		{ADD_HL_R16},	{LD_R16_8},		{DEC16},	{INC8},			{DEC8},		{LD_R8_I8},	{MANIPULATE_CF},
		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},
		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},
		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},
		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},		{LD_R8_R8},	{LD_R8_R8},	{LD_R8_R8},
		{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},
		{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},
		{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},
		{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},		{MATH_R8},	{MATH_R8},		{MATH_R8},	{MATH_R8},	{MATH_R8},
		{RET_CC},	{POP},			{JPCC_I16},		{JP_I16},	{CALLCC_I16},	{PUSH},		{MATH_I8},	{RST},			{RET_CC},		{RET},			{JPCC_I16},		{CB},		{CALLCC_I16},	{CALL},	 	{MATH_I8},	{RST},
		{RET_CC},	{POP},			{JPCC_I16},		{NOP},		{CALLCC_I16},	{PUSH},		{MATH_I8},	{RST},			{RET_CC},		{RET},			{JPCC_I16},		{NOP},		{CALLCC_I16},	{NOP},		{MATH_I8},	{RST},
		{LDH},		{POP},			{LD_IR16_R8},	{NOP},		{NOP},			{PUSH},		{MATH_I8},	{RST},			{ADD_SP_R8},	{JP_HL},		{LD_IR16_R8},	{NOP},		{NOP},			{NOP},		{MATH_I8},	{RST},
		{LDH},		{POP},			{LD_IR16_R8},	{EI_DI},	{NOP},			{PUSH},		{MATH_I8},	{RST},			{LD_HL_SP},		{LD_HL_SP},		{LD_IR16_R8},	{EI_DI},	{NOP},			{NOP},		{MATH_I8},	{RST}
	});
