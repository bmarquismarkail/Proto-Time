#ifndef BMMQ_INTERMEDIATE_REPRESENTATION_HPP
#define BMMQ_INTERMEDIATE_REPRESENTATION_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BMMQ::IR {

using ValueId = std::uint32_t;

enum class ValueType : std::uint8_t {
    Void,
    Bool,
    I8,
    I16,
    I32,
    I64,
};

enum class OperandKind : std::uint8_t {
    Value,
    Immediate,
    GuestRegister,
    GuestAddress,
    BlockTarget,
    Helper,
};

struct Operand {
    OperandKind kind = OperandKind::Immediate;
    ValueType type = ValueType::I64;
    std::uint64_t payload = 0u;

    [[nodiscard]] static constexpr Operand value(ValueId id, ValueType type) noexcept {
        return {.kind = OperandKind::Value, .type = type, .payload = id};
    }
    [[nodiscard]] static constexpr Operand immediate(std::uint64_t value, ValueType type) noexcept {
        return {.kind = OperandKind::Immediate, .type = type, .payload = value};
    }
    [[nodiscard]] static constexpr Operand guestRegister(std::uint32_t id, ValueType type) noexcept {
        return {.kind = OperandKind::GuestRegister, .type = type, .payload = id};
    }
    [[nodiscard]] static constexpr Operand guestAddress(std::uint64_t address, ValueType type) noexcept {
        return {.kind = OperandKind::GuestAddress, .type = type, .payload = address};
    }
    [[nodiscard]] static constexpr Operand blockTarget(std::uint64_t address) noexcept {
        return {.kind = OperandKind::BlockTarget, .type = ValueType::I64, .payload = address};
    }
    [[nodiscard]] static constexpr Operand helper(std::uint32_t id) noexcept {
        return {.kind = OperandKind::Helper, .type = ValueType::I32, .payload = id};
    }
};

enum class Opcode : std::uint8_t {
    Constant,
    ReadRegister,
    WriteRegister,
    LoadMemory,
    StoreMemory,
    Add,
    Subtract,
    Multiply,
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRightLogical,
    ShiftRightArithmetic,
    BitNot,
    CompareEqual,
    CompareNotEqual,
    CompareUnsignedLess,
    CompareSignedLess,
    Select,
    SetProgramCounter,
    Branch,
    BranchIf,
    CallHelper,
    Exit,
    RetireInstruction,
};

enum class MemoryClass : std::uint8_t {
    Generic,
    DirectRam,
    ReadOnly,
    Mmio,
    Cartridge,
};

struct Operation {
    Opcode opcode = Opcode::Exit;
    std::optional<ValueId> result{};
    ValueType resultType = ValueType::Void;
    std::vector<Operand> operands{};
    MemoryClass memoryClass = MemoryClass::Generic;
};

enum class GuardKind : std::uint8_t {
    MappingGeneration,
    CodeBytes,
    ExecutionState,
    HelperAbi,
};

struct Guard {
    GuardKind kind = GuardKind::MappingGeneration;
    std::uint64_t subject = 0u;
    std::uint64_t expected = 0u;
    std::uint64_t mask = ~std::uint64_t{0};
    std::vector<std::uint8_t> bytes{};
};

struct GuestInstruction {
    std::uint64_t address = 0u;
    std::uint8_t length = 0u;
    std::uint32_t cyclesNotTaken = 0u;
    std::uint32_t cyclesTaken = 0u;
    std::optional<ValueId> takenCondition{};
    bool controlFlow = false;
    bool interruptSensitive = false;
    std::vector<Operation> operations{};
};

enum class BlockExit : std::uint8_t {
    Sequential,
    ControlFlow,
    InterruptBoundary,
    Unsupported,
};

struct Block {
    std::uint64_t guestStart = 0u;
    std::uint64_t guestEnd = 0u;
    std::uint64_t mappingGeneration = 0u;
    std::vector<Guard> guards{};
    std::vector<GuestInstruction> instructions{};
    BlockExit exit = BlockExit::Sequential;
};

using BlockPtr = std::shared_ptr<const Block>;

struct ValidationResult {
    bool valid = true;
    std::size_t instructionIndex = 0u;
    std::size_t operationIndex = 0u;
    std::string message{};

    explicit operator bool() const noexcept { return valid; }
};

namespace detail {

[[nodiscard]] inline ValidationResult invalid(
    std::string message,
    std::size_t instructionIndex = 0u,
    std::size_t operationIndex = 0u)
{
    return {
        .valid = false,
        .instructionIndex = instructionIndex,
        .operationIndex = operationIndex,
        .message = std::move(message),
    };
}

[[nodiscard]] constexpr bool requiresResult(Opcode opcode) noexcept
{
    switch (opcode) {
    case Opcode::Constant:
    case Opcode::ReadRegister:
    case Opcode::LoadMemory:
    case Opcode::Add:
    case Opcode::Subtract:
    case Opcode::Multiply:
    case Opcode::BitAnd:
    case Opcode::BitOr:
    case Opcode::BitXor:
    case Opcode::ShiftLeft:
    case Opcode::ShiftRightLogical:
    case Opcode::ShiftRightArithmetic:
    case Opcode::BitNot:
    case Opcode::CompareEqual:
    case Opcode::CompareNotEqual:
    case Opcode::CompareUnsignedLess:
    case Opcode::CompareSignedLess:
    case Opcode::Select:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool forbidsResult(Opcode opcode) noexcept
{
    return opcode != Opcode::CallHelper && !requiresResult(opcode);
}

[[nodiscard]] constexpr bool validOperandCount(Opcode opcode, std::size_t count) noexcept
{
    switch (opcode) {
    case Opcode::Constant:
    case Opcode::ReadRegister:
    case Opcode::LoadMemory:
    case Opcode::BitNot:
    case Opcode::SetProgramCounter:
    case Opcode::Branch:
        return count == 1u;
    case Opcode::WriteRegister:
    case Opcode::StoreMemory:
    case Opcode::Add:
    case Opcode::Subtract:
    case Opcode::Multiply:
    case Opcode::BitAnd:
    case Opcode::BitOr:
    case Opcode::BitXor:
    case Opcode::ShiftLeft:
    case Opcode::ShiftRightLogical:
    case Opcode::ShiftRightArithmetic:
    case Opcode::CompareEqual:
    case Opcode::CompareNotEqual:
    case Opcode::CompareUnsignedLess:
    case Opcode::CompareSignedLess:
    case Opcode::BranchIf:
        return count == 2u;
    case Opcode::Select:
        return count == 3u;
    case Opcode::CallHelper:
        return count >= 1u;
    case Opcode::Exit:
    case Opcode::RetireInstruction:
        return count == 0u;
    }
    return false;
}

[[nodiscard]] constexpr bool isInteger(ValueType type) noexcept
{
    return type == ValueType::I8 || type == ValueType::I16 ||
           type == ValueType::I32 || type == ValueType::I64;
}

} // namespace detail

// Validates the backend-facing invariants. Values are instruction-local: a
// native backend must commit architectural state at RetireInstruction and may
// not carry uncommitted temporaries across that boundary.
[[nodiscard]] inline ValidationResult validate(const Block& block)
{
    if (block.instructions.empty()) {
        return detail::invalid("IR block has no guest instructions");
    }
    if (block.guestStart != block.instructions.front().address) {
        return detail::invalid("IR block start does not match its first instruction");
    }

    std::uint64_t expectedAddress = block.guestStart;
    for (std::size_t instructionIndex = 0u;
         instructionIndex < block.instructions.size(); ++instructionIndex) {
        const auto& instruction = block.instructions[instructionIndex];
        if (instruction.address != expectedAddress) {
            return detail::invalid("IR guest instructions are not contiguous", instructionIndex);
        }
        if (instruction.length == 0u) {
            return detail::invalid("IR guest instruction has zero length", instructionIndex);
        }
        if (instruction.cyclesNotTaken == 0u || instruction.cyclesTaken == 0u) {
            return detail::invalid("IR guest instruction has zero retirement cycles", instructionIndex);
        }
        if (instruction.controlFlow && instructionIndex + 1u != block.instructions.size()) {
            return detail::invalid("control-flow instruction must end an IR block", instructionIndex);
        }
        if (instruction.operations.empty() ||
            instruction.operations.back().opcode != Opcode::RetireInstruction) {
            return detail::invalid("IR guest instruction must end with retirement", instructionIndex);
        }

        std::unordered_map<ValueId, ValueType> values;
        std::size_t retirementCount = 0u;
        std::size_t exitCount = 0u;
        for (std::size_t operationIndex = 0u;
             operationIndex < instruction.operations.size(); ++operationIndex) {
            const auto& operation = instruction.operations[operationIndex];
            if (!detail::validOperandCount(operation.opcode, operation.operands.size())) {
                return detail::invalid("IR operation has invalid operand count",
                                       instructionIndex, operationIndex);
            }
            if (detail::requiresResult(operation.opcode) && !operation.result.has_value()) {
                return detail::invalid("IR operation requires a result",
                                       instructionIndex, operationIndex);
            }
            if (detail::forbidsResult(operation.opcode) && operation.result.has_value()) {
                return detail::invalid("IR operation forbids a result",
                                       instructionIndex, operationIndex);
            }
            if (operation.result.has_value()) {
                if (*operation.result == 0u || operation.resultType == ValueType::Void ||
                    !values.emplace(*operation.result, operation.resultType).second) {
                    return detail::invalid("IR operation has an invalid or duplicate result",
                                           instructionIndex, operationIndex);
                }
            } else if (operation.resultType != ValueType::Void) {
                return detail::invalid("result-less IR operation has a non-void type",
                                       instructionIndex, operationIndex);
            }

            for (const auto& operand : operation.operands) {
                if ((operand.kind == OperandKind::Value ||
                     operand.kind == OperandKind::GuestRegister ||
                     operand.kind == OperandKind::Helper) &&
                    operand.payload > UINT32_MAX) {
                    return detail::invalid("IR operand ID exceeds the host ABI width",
                                           instructionIndex, operationIndex);
                }
                if (operand.kind != OperandKind::Value) continue;
                const auto found = values.find(static_cast<ValueId>(operand.payload));
                if (found == values.end() || found->second != operand.type) {
                    return detail::invalid("IR operation uses an undefined or mistyped value",
                                           instructionIndex, operationIndex);
                }
            }

            const auto operandHasType = [&](std::size_t index, ValueType type) {
                return operation.operands[index].type == type;
            };
            const auto operandIsValueLike = [&](std::size_t index) {
                const auto kind = operation.operands[index].kind;
                return kind == OperandKind::Value || kind == OperandKind::Immediate;
            };
            bool semanticTypesValid = true;
            switch (operation.opcode) {
            case Opcode::Constant:
                semanticTypesValid = operation.operands[0].kind == OperandKind::Immediate &&
                                     operandHasType(0u, operation.resultType);
                break;
            case Opcode::ReadRegister:
                semanticTypesValid = operation.operands[0].kind == OperandKind::GuestRegister &&
                                     operandHasType(0u, operation.resultType);
                break;
            case Opcode::WriteRegister:
                semanticTypesValid = operation.operands[0].kind == OperandKind::GuestRegister &&
                                     operandIsValueLike(1u) &&
                                     operation.operands[0].type == operation.operands[1].type;
                break;
            case Opcode::LoadMemory:
                semanticTypesValid =
                    (operation.operands[0].kind == OperandKind::GuestAddress ||
                     operation.operands[0].kind == OperandKind::Value) &&
                    detail::isInteger(operation.operands[0].type) &&
                    detail::isInteger(operation.resultType);
                break;
            case Opcode::StoreMemory:
                semanticTypesValid =
                    (operation.operands[0].kind == OperandKind::GuestAddress ||
                     operation.operands[0].kind == OperandKind::Value) &&
                    detail::isInteger(operation.operands[0].type) &&
                    operandIsValueLike(1u) && detail::isInteger(operation.operands[1].type);
                break;
            case Opcode::Add:
            case Opcode::Subtract:
            case Opcode::Multiply:
            case Opcode::BitAnd:
            case Opcode::BitOr:
            case Opcode::BitXor:
            case Opcode::ShiftLeft:
            case Opcode::ShiftRightLogical:
            case Opcode::ShiftRightArithmetic:
                semanticTypesValid = detail::isInteger(operation.resultType) &&
                                     operandIsValueLike(0u) && operandIsValueLike(1u) &&
                                     operandHasType(0u, operation.resultType) &&
                                     operandHasType(1u, operation.resultType);
                break;
            case Opcode::BitNot:
                semanticTypesValid = detail::isInteger(operation.resultType) &&
                                     operandIsValueLike(0u) &&
                                     operandHasType(0u, operation.resultType);
                break;
            case Opcode::CompareEqual:
            case Opcode::CompareNotEqual:
            case Opcode::CompareUnsignedLess:
            case Opcode::CompareSignedLess:
                semanticTypesValid = operation.resultType == ValueType::Bool &&
                                     operandIsValueLike(0u) && operandIsValueLike(1u) &&
                                     detail::isInteger(operation.operands[0].type) &&
                                     operation.operands[0].type == operation.operands[1].type;
                break;
            case Opcode::Select:
                semanticTypesValid = operation.operands[0].kind == OperandKind::Value &&
                                     operandHasType(0u, ValueType::Bool) &&
                                     operandIsValueLike(1u) && operandIsValueLike(2u) &&
                                     operandHasType(1u, operation.resultType) &&
                                     operandHasType(2u, operation.resultType);
                break;
            case Opcode::SetProgramCounter:
                semanticTypesValid = operandIsValueLike(0u) &&
                                     detail::isInteger(operation.operands[0].type);
                break;
            case Opcode::Branch:
                semanticTypesValid = operation.operands[0].kind == OperandKind::BlockTarget;
                break;
            case Opcode::BranchIf:
                semanticTypesValid = operation.operands[0].kind == OperandKind::Value &&
                                     operandHasType(0u, ValueType::Bool) &&
                                     operation.operands[1].kind == OperandKind::BlockTarget;
                break;
            case Opcode::CallHelper:
                semanticTypesValid = operation.operands[0].kind == OperandKind::Helper;
                break;
            case Opcode::Exit:
            case Opcode::RetireInstruction:
                break;
            }
            if (!semanticTypesValid) {
                return detail::invalid("IR operation has invalid operand kinds or types",
                                       instructionIndex, operationIndex);
            }
            if (operation.opcode != Opcode::LoadMemory &&
                operation.opcode != Opcode::StoreMemory &&
                operation.memoryClass != MemoryClass::Generic) {
                return detail::invalid("non-memory IR operation has a memory class",
                                       instructionIndex, operationIndex);
            }
            if (operation.opcode == Opcode::RetireInstruction) {
                ++retirementCount;
                if (operationIndex + 1u != instruction.operations.size()) {
                    return detail::invalid("retirement must be the final IR operation",
                                           instructionIndex, operationIndex);
                }
            }
            if (operation.opcode == Opcode::Exit) {
                ++exitCount;
                if (operationIndex + 2u != instruction.operations.size()) {
                    return detail::invalid(
                        "exit must immediately precede retirement",
                        instructionIndex, operationIndex);
                }
            }
        }
        if (retirementCount != 1u) {
            return detail::invalid("IR guest instruction must retire exactly once", instructionIndex);
        }
        if (exitCount > 1u) {
            return detail::invalid("IR guest instruction may exit at most once", instructionIndex);
        }
        if (instruction.takenCondition.has_value()) {
            const auto found = values.find(*instruction.takenCondition);
            if (found == values.end() || found->second != ValueType::Bool) {
                return detail::invalid("cycle selector must name a boolean value", instructionIndex);
            }
        } else if (instruction.cyclesNotTaken != instruction.cyclesTaken) {
            return detail::invalid("variable-cycle instruction requires a cycle selector", instructionIndex);
        }

        if (instruction.length > UINT64_MAX - expectedAddress) {
            return detail::invalid("IR guest instruction address overflows",
                                   instructionIndex);
        }
        expectedAddress += instruction.length;
    }

    if (block.guestEnd != expectedAddress - 1u) {
        return detail::invalid("IR block end does not match its final instruction");
    }
    for (std::size_t guardIndex = 0u; guardIndex < block.guards.size(); ++guardIndex) {
        const auto& guard = block.guards[guardIndex];
        switch (guard.kind) {
        case GuardKind::MappingGeneration:
        case GuardKind::CodeBytes:
        case GuardKind::ExecutionState:
        case GuardKind::HelperAbi:
            break;
        default:
            return detail::invalid("IR guard has an unknown kind");
        }
        if (guard.kind == GuardKind::CodeBytes && guard.bytes.empty()) {
            return detail::invalid("code-byte guard has no expected bytes");
        }
        if (guard.kind != GuardKind::CodeBytes && !guard.bytes.empty()) {
            return detail::invalid("non-code IR guard carries code bytes");
        }
        if (guard.kind == GuardKind::CodeBytes &&
            guard.bytes.size() - 1u > UINT64_MAX - guard.subject) {
            return detail::invalid("code-byte guard address range overflows");
        }
        for (std::size_t priorIndex = 0u; priorIndex < guardIndex; ++priorIndex) {
            const auto& prior = block.guards[priorIndex];
            if (prior.kind == guard.kind && prior.subject == guard.subject &&
                prior.expected == guard.expected && prior.mask == guard.mask &&
                prior.bytes == guard.bytes) {
                return detail::invalid("IR block contains a duplicate guard");
            }
        }
    }
    return {};
}

class BlockBuilder {
public:
    explicit BlockBuilder(std::uint64_t guestStart, std::uint64_t mappingGeneration = 0u)
    {
        block_.guestStart = guestStart;
        block_.mappingGeneration = mappingGeneration;
    }

    BlockBuilder& addGuard(Guard guard) {
        requireNoOpenInstruction("cannot add a guard while building an instruction");
        block_.guards.push_back(std::move(guard));
        return *this;
    }

    BlockBuilder& beginInstruction(
        std::uint64_t address,
        std::uint8_t length,
        std::uint32_t cycles,
        bool controlFlow = false,
        bool interruptSensitive = false)
    {
        return beginInstruction(address, length, cycles, cycles, std::nullopt,
                                controlFlow, interruptSensitive);
    }

    BlockBuilder& beginInstruction(
        std::uint64_t address,
        std::uint8_t length,
        std::uint32_t cyclesNotTaken,
        std::uint32_t cyclesTaken,
        std::optional<ValueId> takenCondition,
        bool controlFlow = false,
        bool interruptSensitive = false)
    {
        requireNoOpenInstruction("an IR instruction is already open");
        block_.instructions.push_back({
            .address = address,
            .length = length,
            .cyclesNotTaken = cyclesNotTaken,
            .cyclesTaken = cyclesTaken,
            .takenCondition = takenCondition,
            .controlFlow = controlFlow,
            .interruptSensitive = interruptSensitive,
        });
        instructionOpen_ = true;
        return *this;
    }

    [[nodiscard]] ValueId emitValue(
        Opcode opcode,
        ValueType type,
        std::vector<Operand> operands = {},
        MemoryClass memoryClass = MemoryClass::Generic)
    {
        requireOpenInstruction();
        const auto id = nextValueId_++;
        currentInstruction().operations.push_back({
            .opcode = opcode,
            .result = id,
            .resultType = type,
            .operands = std::move(operands),
            .memoryClass = memoryClass,
        });
        return id;
    }

    BlockBuilder& emit(
        Opcode opcode,
        std::vector<Operand> operands = {},
        MemoryClass memoryClass = MemoryClass::Generic)
    {
        requireOpenInstruction();
        currentInstruction().operations.push_back({
            .opcode = opcode,
            .operands = std::move(operands),
            .memoryClass = memoryClass,
        });
        return *this;
    }

    BlockBuilder& setTakenCondition(ValueId condition) {
        requireOpenInstruction();
        currentInstruction().takenCondition = condition;
        return *this;
    }

    BlockBuilder& endInstruction() {
        requireOpenInstruction();
        emit(Opcode::RetireInstruction);
        instructionOpen_ = false;
        nextValueId_ = 1u;
        return *this;
    }

    [[nodiscard]] BlockPtr finish(BlockExit exit = BlockExit::Sequential) {
        requireNoOpenInstruction("cannot finish an IR block with an open instruction");
        block_.exit = exit;
        if (!block_.instructions.empty()) {
            const auto& last = block_.instructions.back();
            block_.guestEnd = last.address + last.length - 1u;
        }
        const auto validation = validate(block_);
        if (!validation) {
            throw std::invalid_argument(validation.message);
        }
        return std::make_shared<const Block>(std::move(block_));
    }

private:
    void requireOpenInstruction() const {
        if (!instructionOpen_) {
            throw std::logic_error("no IR instruction is open");
        }
    }

    void requireNoOpenInstruction(const char* message) const {
        if (instructionOpen_) {
            throw std::logic_error(message);
        }
    }

    GuestInstruction& currentInstruction() { return block_.instructions.back(); }

    Block block_{};
    bool instructionOpen_ = false;
    ValueId nextValueId_ = 1u;
};

} // namespace BMMQ::IR

#endif // BMMQ_INTERMEDIATE_REPRESENTATION_HPP
