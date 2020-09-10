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

    // If you want to, you can register a lambda:
    mc.registerMicrocode("test", []() {

    });

    //... or use bind to give it any arguments:
    mc.registerMicrocode("test3", std::bind( &BMMQ::derivedMicrocode::iadd<int>, &mc, a, b ) );

    mc.registerMicrocode("test2", std::bind(
    [](int a, int b) {
        std::cout << a+b << '\n';
    }
    , a,b));

    // After all microcode is created, build the opcode list:
    op.push_microcode(mc, "test3");
    opcodeList.insert(std::make_pair(0,op) );

    BMMQ::fetchBlock<AddressType, DataType> bl = cpu.fetch();
    BMMQ::executionBlock xb = cpu.decode(opcodeList, bl);
    cpu.execute(xb); // should output 4

    return 0;
}
