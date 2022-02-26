

#include "gb_decoder.hpp"
	
	class LR3592_Interpreter_Decode: public LR3592_Decode {
	private:
		BMMQ::MemorySnapshot<AddressType, DataType, AddressType> *snap;
	public:
		//Class Helper Functions
		void useSnapshot(BMMQ::MemorySnapshot<AddressType, DataType, AddressType> *s);
		LR3592_RegisterPair* GetRegister(std::string_view id);
		//Gameboy-Specific Decoding Functions
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
	};