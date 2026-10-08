#include "WholeBlockEmitter.hpp"
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace BMMQ::IR::Research {
namespace {
std::uint64_t mask(ValueType type) {
  switch (type) {
  case ValueType::Bool:
    return 1;
  case ValueType::I8:
    return 255;
  case ValueType::I16:
    return 65535;
  case ValueType::I32:
    return 0xffffffff;
  case ValueType::I64:
    return ~std::uint64_t{0};
  case ValueType::Void:
    return 0;
  }
  throw std::invalid_argument("unknown value type");
}
std::string number(std::uint64_t n) { return std::to_string(n) + "ULL"; }
std::string type(ValueType t) {
  return "static_cast<ValueType>(" + std::to_string(unsigned(t)) + ")";
}
std::string operand(const Operand &op) {
  return op.kind == OperandKind::Value ? "v" + std::to_string(op.payload)
                                       : number(op.payload & mask(op.type));
}
void metadata(std::ostream &s, const Block &b, const std::string &id) {
  s << "CompiledBlock " << id << "(){ auto b=std::make_shared<Block>();\n"
    << "b->guestStart=" << number(b.guestStart)
    << ";b->guestEnd=" << number(b.guestEnd) << ";\n"
    << "b->mappingGeneration=" << number(b.mappingGeneration)
    << ";b->exit=static_cast<BlockExit>(" << unsigned(b.exit) << ");\n";
  for (const auto &g : b.guards) {
    s << "b->guards.push_back({static_cast<GuardKind>(" << unsigned(g.kind)
      << ")," << number(g.subject) << "," << number(g.expected) << ","
      << number(g.mask) << ",{";
    for (auto byte : g.bytes)
      s << unsigned(byte) << ",";
    s << "}});\n";
  }
  for (const auto &i : b.instructions) {
    s << "{GuestInstruction i; i.address=" << number(i.address)
      << ";i.length=" << unsigned(i.length)
      << ";i.cyclesNotTaken=" << i.cyclesNotTaken
      << ";i.cyclesTaken=" << i.cyclesTaken << ";"
      << "i.controlFlow=" << i.controlFlow
      << ";i.interruptSensitive=" << i.interruptSensitive << ";\n";
    if (i.takenCondition)
      s << "i.takenCondition=" << *i.takenCondition << ";\n";
    for (const auto &o : i.operations) {
      s << "{Operation o;o.opcode=static_cast<Opcode>(" << unsigned(o.opcode)
        << ");o.resultType=" << type(o.resultType)
        << ";o.memoryClass=static_cast<MemoryClass>(" << unsigned(o.memoryClass)
        << ");\n";
      if (o.result)
        s << "o.result=" << *o.result << ";\n";
      for (const auto &p : o.operands)
        s << "o.operands.push_back({static_cast<OperandKind>("
          << unsigned(p.kind) << ")," << type(p.type) << ","
          << number(p.payload) << "});\n";
      s << "i.operations.push_back(std::move(o));}\n";
    }
    s << "b->instructions.push_back(std::move(i));}\n";
  }
  s << "return CompiledBlock(std::move(b),&" << id << "_entry);}\n";
}
} // namespace
std::string emitWholeBlock(const Block &block, const std::string &id) {
  if (id.empty() || id.size() > 96 || id.rfind("emitted", 0) != 0 ||
      !std::isalpha(static_cast<unsigned char>(id[0])))
    throw std::invalid_argument("invalid emitted identifier");
  for (unsigned char c : id)
    if (!std::isalnum(c) && c != '_')
      throw std::invalid_argument("invalid emitted identifier");
  const auto valid = validate(block);
  if (!valid)
    throw std::invalid_argument("invalid IR: " + valid.message);
  if (block.instructions.size() >
          IrExecutionService::Limits::kMaxInstructions ||
      block.guards.size() > 16)
    throw std::invalid_argument("emission budget exceeded");
  std::size_t bytes = 0;
  for (const auto &instruction : block.instructions)
    bytes += instruction.length;
  if (bytes > IrExecutionService::Limits::kMaxGuestBytes)
    throw std::invalid_argument("emission byte budget exceeded");
  for (const auto &guard : block.guards)
    if (guard.bytes.size() > IrExecutionService::Limits::kMaxGuestBytes)
      throw std::invalid_argument("emission guard budget exceeded");
  std::ostringstream s;
  s << "#include \"inst_cycle/research/WholeBlock.hpp\"\n#include <array>\n"
    << "namespace BMMQ::IR::Research {\nstatic void " << id
    << "_entry(Invocation& c){ auto& h=c.host(); (void)h;\n";
  for (std::size_t index = 0; index < block.instructions.size(); ++index) {
    const auto &i = block.instructions[index];
    if (i.operations.size() > 64)
      throw std::invalid_argument("emission operation budget exceeded");
    s << "{if(!c.begin(" << index << "))return;InterpreterResult r;\n";
    for (const auto &o : i.operations) {
      if (o.operands.size() > 8 || (o.result && *o.result >= 64))
        throw std::invalid_argument("emission value budget exceeded");
      const auto arg = [&](unsigned n) { return operand(o.operands.at(n)); };
      std::string expr;
      bool statement = false;
      switch (o.opcode) {
      case Opcode::Constant:
        expr = arg(0);
        break;
      case Opcode::ReadRegister:
        expr = "h.readRegister(" + number(o.operands[0].payload) + "," +
               type(o.resultType) + ")";
        break;
      case Opcode::WriteRegister:
        expr = "h.writeRegister(" + number(o.operands[0].payload) + "," +
               type(o.operands[0].type) + "," + arg(1) + ")";
        statement = true;
        break;
      case Opcode::LoadMemory:
        expr = "h.loadMemory(" + arg(0) + "," + type(o.resultType) +
               ",static_cast<MemoryClass>(" +
               std::to_string(unsigned(o.memoryClass)) + "))";
        break;
      case Opcode::StoreMemory:
        expr = "h.storeMemory(" + arg(0) + "," + type(o.operands[1].type) +
               ",static_cast<MemoryClass>(" +
               std::to_string(unsigned(o.memoryClass)) + ")," + arg(1) + ")";
        statement = true;
        break;
      case Opcode::Add:
        expr = arg(0) + "+" + arg(1);
        break;
      case Opcode::Subtract:
        expr = arg(0) + "-" + arg(1);
        break;
      case Opcode::Multiply:
        expr = arg(0) + "*" + arg(1);
        break;
      case Opcode::BitAnd:
        expr = arg(0) + "&" + arg(1);
        break;
      case Opcode::BitOr:
        expr = arg(0) + "|" + arg(1);
        break;
      case Opcode::BitXor:
        expr = arg(0) + "^" + arg(1);
        break;
      case Opcode::BitNot:
        expr = "~" + arg(0);
        break;
      case Opcode::CompareEqual:
        expr = arg(0) + "==" + arg(1);
        break;
      case Opcode::CompareNotEqual:
        expr = arg(0) + "!=" + arg(1);
        break;
      case Opcode::CompareUnsignedLess:
        expr = arg(0) + "<" + arg(1);
        break;
      case Opcode::Select:
        expr = arg(0) + "?" + arg(1) + ":" + arg(2);
        break;
      case Opcode::SetProgramCounter:
        expr = "h.setProgramCounter(" + arg(0) + ")";
        statement = true;
        break;
      case Opcode::Branch:
        expr = "h.setProgramCounter(" + number(o.operands[0].payload) +
               ");r.branchTaken=true";
        statement = true;
        break;
      case Opcode::BranchIf:
        expr = "if(" + arg(0) + "){h.setProgramCounter(" +
               number(o.operands[1].payload) + ");r.branchTaken=true;}";
        statement = true;
        break;
      case Opcode::CallHelper: {
        // Fixed stack arguments; no runtime operation or value dispatch.
        const auto n = o.operands.size() - 1;
        s << "std::array<std::uint64_t," << n << "> args" << index << "_"
          << (&o - i.operations.data()) << "{";
        for (std::size_t k = 1; k < o.operands.size(); ++k)
          s << arg(k) << ",";
        s << "};\n";
        expr = "h.callHelper(" + number(o.operands[0].payload) + "," +
               type(o.resultType) + ",args" + std::to_string(index) + "_" +
               std::to_string(&o - i.operations.data()) + ")";
        statement = !o.result;
        break;
      }
      case Opcode::Exit:
        expr = "r.exitRequested=true";
        statement = true;
        break;
      case Opcode::RetireInstruction:
        expr = "r.retirementReached=true";
        statement = true;
        break;
      // Deliberately bounded pilot: do not expand before design review.
      default:
        throw std::invalid_argument("opcode outside whole-block pilot");
      }
      if (statement)
        s << "if(!r.exitRequested || "
          << (o.opcode == Opcode::RetireInstruction) << "){" << expr << ";}\n";
      else {
        if (!o.result)
          throw std::invalid_argument("missing emitted result");
        s << "const std::uint64_t v" << *o.result
          << "=r.exitRequested?0ULL:(static_cast<std::uint64_t>(" << expr
          << ")&" << number(mask(o.resultType)) << ");(void)v" << *o.result
          << ";\n";
      }
    }
    if (i.takenCondition)
      s << "r.cycleCondition=v" << *i.takenCondition << "!=0;\n";
    s << "if(!c.retire(" << index << ",r))return;}\n";
  }
  s << "}\n";
  metadata(s, block, id);
  s << "}\n";
  return s.str();
}
} // namespace BMMQ::IR::Research
