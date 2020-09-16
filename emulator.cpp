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
#include <cstdlib>

#include "cores/gameboy.hpp"

bool failure(std::string message){
	std::cout << message;
	return true;
}

int main(int argc, char** argv)
{
    LR3592_DMG cpu;

	// Test if all register pointers are null:
	bool failed = false;
	if (cpu.file.findRegister("AF")->second == nullptr)
		failed = failure("Register AF is null. Failure.");

	if (cpu.file.findRegister("BC")->second == nullptr)
		failed = failure("Register BC is null. Failure.");

	if (cpu.file.findRegister("DE")->second == nullptr)
		failed = failure("Register DE is null. Failure.");

	if (cpu.file.findRegister("HL")->second == nullptr)
		failed = failure("Register HL is null. Failure.");

	if (cpu.file.findRegister("SP")->second == nullptr)
		failed = failure("Register SP is null. Failure.");
	
    // Test if RegisterInfo structs are null;
	
	if (cpu.AF() == nullptr)
		failed = failure("RegisterInfo AF is null. Failure.");

	if (cpu.BC() == nullptr)
		failed = failure("RegisterInfo BC is null. Failure.");

	if (cpu.DE() == nullptr)
		failed = failure("RegisterInfo DE is null. Failure.");

	if (cpu.HL() == nullptr)
		failed = failure("RegisterInfo HL is null. Failure.");

	if (cpu.SP() == nullptr)
		failed = failure("RegisterInfo SP is null. Failure.");

	// Test if value assigned to RegisterInfo reflects actual register value.
	
 	cpu.AF()->value = std::rand();
	if (cpu.file.findRegister("AF")->second->value != cpu.AF()->value ) {
		std::cout << "AF Value:" << cpu.AF()->value << '\n';
		std::cout << "Register File Value:" << cpu.file.findRegister("AF")->second->value << '\n';
		failed = failure("RegisterInfo AF does not reflect register in file. Failure.");
	}

 	cpu.BC()->value = std::rand();
	if (cpu.file.findRegister("BC")->second->value != cpu.BC()->value ) {
		std::cout << "BC Value:" << cpu.BC()->value << '\n';
		std::cout << "Register File Value:" << cpu.file.findRegister("BC")->second->value << '\n';
		failed = failure("RegisterInfo BC does not reflect register in file. Failure.");
	}

 	cpu.DE()->value = std::rand();
	if (cpu.file.findRegister("DE")->second->value != cpu.DE()->value ) {
		std::cout << "DE Value:" << cpu.DE()->value << '\n';
		std::cout << "Register File Value:" << cpu.file.findRegister("DE")->second->value << '\n';
		failed = failure("RegisterInfo DE does not reflect register in file. Failure.");
	}

 	cpu.HL()->value = std::rand();
	if (cpu.file.findRegister("HL")->second->value != cpu.HL()->value ) {
		std::cout << "HL Value:" << cpu.HL()->value << '\n';
		std::cout << "Register File Value:" << cpu.file.findRegister("HL")->second->value << '\n';
		failed = failure("RegisterInfo HL does not reflect register in file. Failure.");
	}

 	cpu.SP()->value = std::rand();
	if (cpu.file.findRegister("SP")->second->value != cpu.SP()->value ) {
		std::cout << "SP Value:" << cpu.SP()->value << '\n';
		std::cout << "Register File Value:" << cpu.file.findRegister("SP")->second->value << '\n';
		failed = failure("RegisterInfo SP does not reflect register in file. Failure.");
	}
	
	return 0;
}
