#ifndef BMMQ_INTERMEDIATE_REPRESENTATION_INTERPRETER_HPP
#define BMMQ_INTERMEDIATE_REPRESENTATION_INTERPRETER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "IntermediateRepresentation.hpp"

namespace BMMQ::IR {

class InterpreterHost {
public:
    virtual ~InterpreterHost() = default;
    virtual std::uint64_t readRegister(std::uint32_t id, ValueType type) = 0;
    virtual void writeRegister(std::uint32_t id, ValueType type, std::uint64_t value) = 0;
    virtual std::uint64_t loadMemory(std::uint64_t address, ValueType type, MemoryClass memoryClass) = 0;
    virtual void storeMemory(std::uint64_t address, ValueType type, MemoryClass memoryClass,
                             std::uint64_t value) = 0;
    virtual std::uint64_t callHelper(std::uint32_t id, ValueType resultType,
                                     std::span<const std::uint64_t> arguments) = 0;
    virtual void setProgramCounter(std::uint64_t address) = 0;
};

struct InterpreterResult {
    bool branchTaken = false;
    bool exitRequested = false;
    bool cycleCondition = false;
};

class Interpreter {
public:
    InterpreterResult execute(const GuestInstruction& instruction, InterpreterHost& host)
    {
        ValueId maxValue = 0u;
        for (const auto& operation : instruction.operations) {
            if (operation.result.has_value()) maxValue = std::max(maxValue, *operation.result);
        }
        values_.assign(static_cast<std::size_t>(maxValue) + 1u, 0u);

        InterpreterResult result;
        for (const auto& operation : instruction.operations) {
            const auto value = executeOperation(operation, host, result);
            if (operation.result.has_value()) {
                values_[*operation.result] = truncate(value, operation.resultType);
            }
        }
        if (instruction.takenCondition.has_value()) {
            result.cycleCondition = values_.at(*instruction.takenCondition) != 0u;
        }
        return result;
    }

private:
    static constexpr std::uint64_t mask(ValueType type) noexcept
    {
        switch (type) {
        case ValueType::Bool: return 0x1u;
        case ValueType::I8: return 0xFFu;
        case ValueType::I16: return 0xFFFFu;
        case ValueType::I32: return 0xFFFFFFFFu;
        case ValueType::I64: return ~std::uint64_t{0};
        case ValueType::Void: return 0u;
        }
        return 0u;
    }

    static constexpr std::uint64_t truncate(std::uint64_t value, ValueType type) noexcept
    {
        return value & mask(type);
    }

    static constexpr std::int64_t signedValue(std::uint64_t value, ValueType type) noexcept
    {
        switch (type) {
        case ValueType::Bool: return static_cast<std::int64_t>(value & 1u);
        case ValueType::I8: return static_cast<std::int8_t>(value);
        case ValueType::I16: return static_cast<std::int16_t>(value);
        case ValueType::I32: return static_cast<std::int32_t>(value);
        case ValueType::I64: return static_cast<std::int64_t>(value);
        case ValueType::Void: return 0;
        }
        return 0;
    }

    std::uint64_t operandValue(const Operand& operand) const
    {
        if (operand.kind == OperandKind::Value) {
            return values_.at(static_cast<ValueId>(operand.payload));
        }
        return truncate(operand.payload, operand.type);
    }

    std::uint64_t executeOperation(const Operation& operation,
                                   InterpreterHost& host,
                                   InterpreterResult& result)
    {
        const auto binary = [&](auto&& function) {
            return function(operandValue(operation.operands[0]),
                            operandValue(operation.operands[1]));
        };

        switch (operation.opcode) {
        case Opcode::Constant:
            return operandValue(operation.operands[0]);
        case Opcode::ReadRegister:
            return host.readRegister(static_cast<std::uint32_t>(operation.operands[0].payload),
                                     operation.resultType);
        case Opcode::WriteRegister:
            host.writeRegister(static_cast<std::uint32_t>(operation.operands[0].payload),
                               operation.operands[0].type,
                               operandValue(operation.operands[1]));
            return 0u;
        case Opcode::LoadMemory:
            return host.loadMemory(operandValue(operation.operands[0]), operation.resultType,
                                   operation.memoryClass);
        case Opcode::StoreMemory:
            host.storeMemory(operandValue(operation.operands[0]), operation.operands[1].type,
                             operation.memoryClass, operandValue(operation.operands[1]));
            return 0u;
        case Opcode::Add: return binary([](auto lhs, auto rhs) { return lhs + rhs; });
        case Opcode::Subtract: return binary([](auto lhs, auto rhs) { return lhs - rhs; });
        case Opcode::Multiply: return binary([](auto lhs, auto rhs) { return lhs * rhs; });
        case Opcode::BitAnd: return binary([](auto lhs, auto rhs) { return lhs & rhs; });
        case Opcode::BitOr: return binary([](auto lhs, auto rhs) { return lhs | rhs; });
        case Opcode::BitXor: return binary([](auto lhs, auto rhs) { return lhs ^ rhs; });
        case Opcode::ShiftLeft: return binary([](auto lhs, auto rhs) { return lhs << (rhs & 63u); });
        case Opcode::ShiftRightLogical:
            return binary([](auto lhs, auto rhs) { return lhs >> (rhs & 63u); });
        case Opcode::ShiftRightArithmetic:
            return static_cast<std::uint64_t>(
                signedValue(operandValue(operation.operands[0]), operation.operands[0].type) >>
                (operandValue(operation.operands[1]) & 63u));
        case Opcode::BitNot: return ~operandValue(operation.operands[0]);
        case Opcode::CompareEqual:
            return binary([](auto lhs, auto rhs) { return lhs == rhs ? 1u : 0u; });
        case Opcode::CompareNotEqual:
            return binary([](auto lhs, auto rhs) { return lhs != rhs ? 1u : 0u; });
        case Opcode::CompareUnsignedLess:
            return binary([](auto lhs, auto rhs) { return lhs < rhs ? 1u : 0u; });
        case Opcode::CompareSignedLess:
            return signedValue(operandValue(operation.operands[0]), operation.operands[0].type) <
                           signedValue(operandValue(operation.operands[1]), operation.operands[1].type)
                       ? 1u : 0u;
        case Opcode::Select:
            return operandValue(operation.operands[0]) != 0u
                ? operandValue(operation.operands[1])
                : operandValue(operation.operands[2]);
        case Opcode::SetProgramCounter:
            host.setProgramCounter(operandValue(operation.operands[0]));
            return 0u;
        case Opcode::Branch:
            host.setProgramCounter(operation.operands[0].payload);
            result.branchTaken = true;
            return 0u;
        case Opcode::BranchIf:
            if (operandValue(operation.operands[0]) != 0u) {
                host.setProgramCounter(operation.operands[1].payload);
                result.branchTaken = true;
            }
            return 0u;
        case Opcode::CallHelper: {
            helperArguments_.clear();
            helperArguments_.reserve(operation.operands.size() - 1u);
            for (std::size_t index = 1u; index < operation.operands.size(); ++index) {
                helperArguments_.push_back(operandValue(operation.operands[index]));
            }
            return host.callHelper(static_cast<std::uint32_t>(operation.operands[0].payload),
                                   operation.resultType, helperArguments_);
        }
        case Opcode::Exit:
            result.exitRequested = true;
            return 0u;
        case Opcode::RetireInstruction:
            return 0u;
        }
        throw std::invalid_argument("unknown portable IR opcode");
    }

    std::vector<std::uint64_t> values_{};
    std::vector<std::uint64_t> helperArguments_{};
};

} // namespace BMMQ::IR

#endif // BMMQ_INTERMEDIATE_REPRESENTATION_INTERPRETER_HPP
