#include "opcode.hpp"

namespace BMMQ {
	
	std::vector<IOpcode>& executionBlock::getBlock() { return code; }
	const std::vector<IOpcode>& executionBlock::getBlock() const { return code; }
}