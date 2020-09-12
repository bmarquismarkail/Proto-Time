/////////////////////////////////////////////////////////////////////////
//
//	2020 Emulator Project Idea Mk 2
//	Author: Brandon M. M. Green
//
//	/////
//
// 	The purpose of this is to see the vision of this emulator system
//
//
/////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <vector>

#include "cores/gameboy.hpp"

int main(int argc, char** argv)
{

    int a = 2;
    int b = 2;
    BMMQ::derivedMicrocode mc;
    BMMQ::IOpcode op;
    BMMQ::OpcodeList<uint8_t> opcodeList;
    LR3592_DMG cpu;

    BMMQ::fetchBlock<AddressType, DataType> bl = cpu.fetch();
    BMMQ::executionBlock xb = cpu.decode(opcodeList, bl);
    cpu.execute(xb);
	//std:: cout << cpu.AF.lo;

    return 0;
}
