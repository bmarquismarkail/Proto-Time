#include "gb_interpreter.hpp"

DataType* LR3592_Interpreter_Decode::readTempByte(AddressType address)
{
	scratchToggle = !scratchToggle;
	DataType* target = scratchToggle ? &scratchReadA : &scratchReadB;
	snap->mem.read(target, address, 1);
	return target;
}

void LR3592_Interpreter_Decode::setSnapshot(BMMQ::MemorySnapshot<AddressType, DataType, AddressType> *s)
{
	snap = s;
}


LR3592_Register* LR3592_Interpreter_Decode::GetRegister(std::string_view id)
{
	auto& regfile = cpu->getMemory().file;
	snap->copyRegisterFromMainFile(id, regfile);
	auto* entry = snap->file.findRegister(id);
	if (entry == nullptr || entry->reg == nullptr) {
		throw std::invalid_argument("register not found");
	}
	return entry->reg.get();
}

LR3592_RegisterPair* LR3592_Interpreter_Decode::GetPairRegister(std::string_view id)
{
	auto* reg = GetRegister(id);
	auto* pairReg = dynamic_cast<LR3592_RegisterPair*>(reg);
	if (pairReg == nullptr) {
		throw std::invalid_argument("register is not a pair");
	}
	return pairReg;
}

bool LR3592_Interpreter_Decode::checkJumpCond(DataType opcode)
{
	auto A = GetPairRegister("AF");
	bool checkFlag	= (opcode & 0x10) == 0x10 ? ((A->lo & 0x80) >> 7) : ((A->lo & 0x10) >> 4);
	bool checkSet = opcode & 0x8;
	return !(checkFlag ^ checkSet);
}

AddressType* LR3592_Interpreter_Decode::ld_R16_I16_GetRegister(DataType opcode)
{


	uint8_t regInd = (opcode & 0x30) >> 4;

	switch (regInd) {
	case 0:
		return &(GetPairRegister("BC")->value);
	case 1:
		return &(GetPairRegister("DE")->value);
	case 2:
		return &(GetPairRegister("HL")->value);
	case 3:
		return &(GetRegister("SP")->value);
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}
}

AddressType* LR3592_Interpreter_Decode::add_HL_r16_GetRegister(DataType opcode)
{
	return ld_R16_I16_GetRegister(opcode);
}

AddressType* LR3592_Interpreter_Decode::ld_r16_8_GetRegister(DataType opcode)
{
	switch ((opcode & 0x30) >> 4) {
	case 0:
		return &(GetPairRegister("BC")->value);
	case 1:
		return &(GetPairRegister("DE")->value);
	case 2:
	case 3:
		return &(GetPairRegister("HL")->value);
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}
}

// LD ir16/8 <-> A
std::pair<DataType*, DataType*> LR3592_Interpreter_Decode::ld_r16_8_GetOperands(DataType opcode)
{
	AddressType *reg16 = ld_R16_I16_GetRegister(opcode);
	DataType *accumulator = &GetPairRegister("AF")->hi;
	DataType *temp = readTempByte(*reg16);

	DataType srcFlag = (opcode & 8) >> 3;
	switch(srcFlag) {
	case 0: // Accumulator Source
		return std::make_pair(temp, accumulator);
	case 1: // Accumulator Destination
		return std::make_pair(accumulator, temp);
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}
}

DataType* LR3592_Interpreter_Decode::ld_r8_i8_GetRegister(DataType opcode)
{
	auto regSet = (opcode & 0x38) >> 3;
	switch (regSet) {
	case 0:
		return &(GetPairRegister("BC")->hi);
	case 1:
		return &GetPairRegister("BC")->lo;
	case 2:
		return &GetPairRegister("DE")->hi;
	case 3:
		return &GetPairRegister("DE")->lo;
	case 4:
		return &GetPairRegister("HL")->hi;
	case 5:
		return &GetPairRegister("HL")->lo;
	case 6:
		{
			auto HL = GetPairRegister("HL");
			return readTempByte(HL->value);
		}
	case 7:
		return &(GetPairRegister("AF")->hi);
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}
}

void LR3592_Interpreter_Decode::rotateAccumulator(DataType opcode)
{

	auto A = GetPairRegister("AF");

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

	auto A =GetPairRegister("AF")->value;
	DataType opSet = (opcode & 8) >> 3;

	switch (opSet) {
	case 0:
		BMMQ::CML::daa(&A);
		break;
	case 1:
		BMMQ::CML::cpl(&A);
		break;
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}
}

// Incomplete. need	to build the mask
void LR3592_Interpreter_Decode::manipulateCarry(DataType opcode)
{
	DataType opSet =  (opcode & 0x8) >> 3;

	auto *F = &(GetPairRegister("AF")->lo);
	DataType flags = *F;

	switch (opSet) {
	case 0:
		flags |= (1 << 4);
		break;
	case 1:
		flags &= ~(1 << 4);
		break;
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}

	BMMQ::CML::setFlags(F, flags);
}

DataType* LR3592_Interpreter_Decode::ld_r8_r8_GetRegister(DataType regcode)
{
	switch (regcode) {
	case 0:
		return &(GetPairRegister("BC")->hi);
	case 1:
		return &(GetPairRegister("BC")->lo);
	case 2:
		return &(GetPairRegister("DE")->hi);
	case 3:
		return &(GetPairRegister("DE")->lo);
	case 4:
		return &(GetPairRegister("HL")->hi);
	case 5:
		return &(GetPairRegister("HL")->lo);
	case 6:
		{
			auto HL = GetPairRegister("HL");
			return readTempByte(HL->value);
		}
	case 7:
		return &(GetPairRegister("AF")->hi);
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
	}
}

std::pair<DataType*, DataType*> LR3592_Interpreter_Decode::ld_r8_r8_GetOperands(DataType opcode)
{
	DataType destReg = (opcode & 0x38) >> 3;
	DataType srcReg = (opcode & 7);

	return std::make_pair(ld_r8_r8_GetRegister(destReg), ld_r8_r8_GetRegister(srcReg) );
}

void LR3592_Interpreter_Decode::math_r8(DataType opcode)
{
	DataType mathFunc = (opcode & 0x38) >> 3;
	DataType* srcReg = ld_r8_r8_GetRegister(opcode & 7);

	auto A = GetPairRegister("AF");
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
	DataType mathFunc = (opcode & 0x38) >> 3;

	auto A = GetPairRegister("AF");
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
		return &(GetPairRegister("BC")->value);
	case 1:
		return &(GetPairRegister("DE")->value);
	case 2:
		return &(GetPairRegister("HL")->value);
	case 3:
		return &(GetPairRegister("AF")->value);
	default:
		throw std::invalid_argument("error in decoding register. invalid argument");
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

// This is an issue
void LR3592_Interpreter_Decode::ldh(DataType opcode)
{
	DataType* dest = nullptr;
	DataType* src = nullptr;

	// Check if the Accumulator is the source
	auto srcSet = !( (opcode & 0x10) >> 4);

	auto A = GetPairRegister("AF");
	auto mdr = GetRegister("mdr");
	if (srcSet) {
		src = &A->hi;
		DataType temp = 0;
		dest = &temp;
		BMMQ::CML::loadtmp(dest, *src);
		snap->mem.write(dest, mdr->value + 0xFF00, 1);
	}
	else {
		dest = &A->hi;
		DataType temp = 0;
		src = &temp;
		snap->mem.read(src, mdr->value + 0xFF00, 1);
		BMMQ::CML::loadtmp(dest, *src);
	}
}

void LR3592_Interpreter_Decode::ld_ir16_r8(DataType opcode)
{
	DataType* dest;
	DataType* src;

	auto regSet = (opcode & 8)	>> 3;
	auto srcSet = (opcode & 0x10) >> 4;

	auto A = GetPairRegister("AF");
	auto C = GetPairRegister("BC");
	auto mdr = GetRegister("mdr");
	switch	(srcSet) {
	case 0: {
		src = &A->hi;
		DataType temp = 0;
		dest = &temp;
		BMMQ::CML::loadtmp(dest, src);
		snap->mem.write(dest, regSet ? ( C->lo + 0xFF00 ) : mdr->value, 1);
		break;
	}
	case 1: {
		DataType temp = 0;
		src = &temp;
		snap->mem.read(src, regSet ? ( C->lo + 0xFF00 ) : mdr->value, 1);
		dest = &A->hi;
		BMMQ::CML::loadtmp(dest, src);
		break;
	}
	}
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
		dest = &(GetPairRegister("HL")->value);
		src = &(GetRegister("SP")->value);
		*src += static_cast<DataType>(mdr->value & 0x00ff);
		break;
	case 1:
		dest = &(GetRegister("SP")->value);
		src = &(GetPairRegister("HL")->value);
		break;
	}

	BMMQ::CML::loadtmp(dest, src);
}

void LR3592_Interpreter_Decode::cb_execute(DataType opcode)
{
	auto operation	= ( opcode & 0xF8 ) >> 3;
	DataType testBit = (opcode & 0x38) >> 3;
	auto reg = ( opcode & 7 );
	auto dest = ld_r8_r8_GetRegister(reg);

	auto quadrant = opcode >> 6;

	DataType tempFlag;
	auto &F = GetPairRegister("AF")->lo;
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

	auto &A = GetPairRegister("AF")->hi;
	auto &F = GetPairRegister("AF")->lo;
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

void LR3592_Interpreter_Decode::populateOpcodes(){}
