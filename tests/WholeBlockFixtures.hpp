#pragma once
#include "cores/gameboy/GameBoyIrExecution.hpp"
#include "cores/gamegear/GameGearIrExecution.hpp"
#include "inst_cycle/research/WholeBlock.hpp"

namespace BMMQ::IR::Research {
CompiledBlock emittedGameBoyCompute();
CompiledBlock emittedGameGearCompute();
CompiledBlock emittedGameBoyRam();
CompiledBlock emittedGameGearRam();
CompiledBlock emittedControl();
CompiledBlock emittedGameBoyRamCode();
CompiledBlock emittedGameGearRamCode();
CompiledBlock emittedGameBoyBank();
CompiledBlock emittedGameGearBank();
CompiledBlock emittedGameBoyBankBoundary();
CompiledBlock emittedGameGearBankBoundary();

inline BlockPtr fixture(bool gameBoy, bool ram,
                        std::optional<std::uint16_t> start = std::nullopt) {
  const std::vector<std::vector<std::uint8_t>> bytes =
      ram ? std::vector<std::vector<std::uint8_t>>{{0x77}, {0x34}, {0x7e},
                                                   {0x35}, {0x86}, {0x18, 0xf9}}
          : std::vector<std::vector<std::uint8_t>>{{0x06, 0xff}, {0x04}, {0x48},
                                                   {0x81},       {0x0d}, {0xaf},
                                                   {0x18, 0xf7}};
  std::vector<SourceInstruction> source;
  std::uint64_t pc = start.value_or(gameBoy ? 0x100 : 0);
  for (const auto &b : bytes) {
    SourceInstruction i{.address = pc,
                        .length = static_cast<std::uint8_t>(b.size())};
    std::copy(b.begin(), b.end(), i.bytes.begin());
    source.push_back(i);
    pc += b.size();
  }
  LoweringRequest request{
      .instructions = source, .mappingGeneration = 7, .executionState = 0};
  std::string error;
  GB::IRExecution::GameBoyCoreAdapter gb;
  GameGearIR::CoreAdapter gg;
  auto &adapter = gameBoy ? static_cast<IIrCoreAdapter &>(gb)
                          : static_cast<IIrCoreAdapter &>(gg);
  auto block = adapter.lower(request, &error);
  if (!block || !adapter.validateLoweredBlock(request, *block))
    throw std::runtime_error("pilot lowering failed: " + error);
  return block;
}
inline BlockPtr controlFixture() {
  BlockBuilder b(0x100, 7);
  b.beginInstruction(0x100, 1, 4);
  auto a = b.emitValue(Opcode::ReadRegister, ValueType::I8,
                       {Operand::guestRegister(0, ValueType::I8)});
  auto v = b.emitValue(Opcode::Constant, ValueType::I8,
                       {Operand::immediate(0xff, ValueType::I8)});
  auto mul = b.emitValue(
      Opcode::Multiply, ValueType::I8,
      {Operand::value(a, ValueType::I8), Operand::value(v, ValueType::I8)});
  auto inv = b.emitValue(Opcode::BitNot, ValueType::I8,
                         {Operand::value(mul, ValueType::I8)});
  auto andValue = b.emitValue(
      Opcode::BitAnd, ValueType::I8,
      {Operand::value(inv, ValueType::I8), Operand::value(a, ValueType::I8)});
  auto orValue = b.emitValue(Opcode::BitOr, ValueType::I8,
                             {Operand::value(andValue, ValueType::I8),
                              Operand::value(v, ValueType::I8)});
  auto xorValue = b.emitValue(Opcode::BitXor, ValueType::I8,
                              {Operand::value(orValue, ValueType::I8),
                               Operand::value(a, ValueType::I8)});
  auto eq = b.emitValue(
      Opcode::CompareEqual, ValueType::Bool,
      {Operand::value(a, ValueType::I8), Operand::immediate(0, ValueType::I8)});
  auto ne = b.emitValue(
      Opcode::CompareNotEqual, ValueType::Bool,
      {Operand::value(a, ValueType::I8), Operand::immediate(0, ValueType::I8)});
  auto lt = b.emitValue(Opcode::CompareUnsignedLess, ValueType::Bool,
                        {Operand::value(a, ValueType::I8),
                         Operand::immediate(128, ValueType::I8)});
  auto selected = b.emitValue(Opcode::Select, ValueType::I8,
                              {Operand::value(lt, ValueType::Bool),
                               Operand::value(xorValue, ValueType::I8),
                               Operand::value(mul, ValueType::I8)});
  b.emit(Opcode::WriteRegister, {Operand::guestRegister(1, ValueType::I8),
                                 Operand::value(selected, ValueType::I8)});
  b.emit(Opcode::WriteRegister, {Operand::guestRegister(2, ValueType::Bool),
                                 Operand::value(eq, ValueType::Bool)});
  b.emit(Opcode::WriteRegister, {Operand::guestRegister(3, ValueType::Bool),
                                 Operand::value(ne, ValueType::Bool)});
  b.emit(Opcode::SetProgramCounter,
         {Operand::immediate(0x101, ValueType::I16)});
  b.endInstruction();
  b.beginInstruction(0x101, 1, 4, 8, std::nullopt, true);
  auto condition = b.emitValue(Opcode::ReadRegister, ValueType::Bool,
                               {Operand::guestRegister(2, ValueType::Bool)});
  b.setTakenCondition(condition);
  b.emit(Opcode::BranchIf, {Operand::value(condition, ValueType::Bool),
                            Operand::blockTarget(0x200)});
  b.emit(Opcode::Exit, {});
  b.endInstruction();
  return b.finish();
}
} // namespace BMMQ::IR::Research
