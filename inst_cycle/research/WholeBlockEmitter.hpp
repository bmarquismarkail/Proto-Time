#pragma once
#include "inst_cycle/IrExecutionService.hpp"
#include <string>
namespace BMMQ::IR::Research {
// Produces a standalone translation unit. All source operands become numeric
// literals; the only caller text, a C++ identifier starting with emitted, is
// checked before emission. Run outside the machine lane. Unsupported inputs
// fail before compilation.
std::string emitWholeBlock(const Block &block, const std::string &identifier,
                           std::uint32_t architecture = kAnyArchitecture);
} // namespace BMMQ::IR::Research
