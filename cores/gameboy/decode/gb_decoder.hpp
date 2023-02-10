#include "../gameboy.hpp"

	class LR3592_Decode {
	protected:
		LR3592_DMG *cpu;
	public:
		//ctors and dtors
		LR3592_Decode(LR3592_DMG *c);
		// Class Helpers
		virtual LR3592_Register &GetRegister(BMMQ::RegisterInfo<AddressType>& Reg) = 0;
		// Gameboy-Specific Decoding Functions
		virtual bool checkJumpCond(DataType opcode) = 0;
		virtual AddressType* ld_R16_I16_GetRegister(DataType opcode) = 0;
		virtual AddressType* add_HL_r16_GetRegister(DataType opcode) = 0;
		virtual AddressType* ld_r16_8_GetRegister(DataType opcode) = 0;
		virtual std::pair<DataType*, DataType*>  ld_r16_8_GetOperands(DataType opcode) = 0;
		virtual DataType* ld_r8_i8_GetRegister(DataType opcode) = 0;
		virtual void rotateAccumulator(DataType opcode) = 0;
		virtual void manipulateAccumulator(DataType opcode) = 0;
		virtual void manipulateCarry(DataType opcode) = 0;
		virtual DataType* ld_r8_r8_GetRegister(DataType regcode) = 0;
		virtual std::pair<DataType*, DataType*> ld_r8_r8_GetOperands(DataType opcode) = 0;
		virtual void math_r8(DataType opcode) = 0;
		virtual void math_i8(DataType opcode) = 0;
		virtual void ret() = 0;
		virtual void ret_cc(DataType opcode) = 0;
		virtual AddressType* push_pop_GetRegister(DataType opcode) = 0;
		virtual void pop(DataType opcode) = 0;
		virtual void push(DataType opcode) = 0;
		virtual void call() = 0;
		virtual void call_cc(DataType opcode) = 0;
		virtual void rst(DataType opcode) = 0;
		virtual void ldh(DataType opcode) = 0;
		virtual void ld_ir16_r8(DataType opcode) = 0;
		virtual void ei_di(DataType opcode) = 0;
		virtual void ld_hl_sp(DataType opcode) = 0;
		virtual void cb_execute(DataType opcode) = 0;
		virtual void calculateflags(uint16_t calculationFlags) = 0;
		virtual void nop() = 0;
		virtual void stop() = 0;
		virtual void populateOpcodes() = 0;
	};	//LR3592_Decode