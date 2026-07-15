#include "GameBoyNativeExecution.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(__x86_64__) && (defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__))
#include <sys/mman.h>
#include <unistd.h>
#define BMMQ_GAMEBOY_NATIVE_X64 1
#else
#define BMMQ_GAMEBOY_NATIVE_X64 0
#endif

namespace GB::NativeExecution {
namespace {

constexpr std::size_t kMaxOperations = 64u;
constexpr std::size_t kMaxOperands = 8u;
constexpr std::size_t kMaxValues = 64u;
constexpr std::size_t kThunkBytes = 25u;

struct PlannedOperation {
    BMMQ::IR::Opcode opcode{};
    BMMQ::IR::ValueType resultType{};
    BMMQ::IR::MemoryClass memoryClass{};
    std::uint32_t result = 0u;
    std::uint8_t operandCount = 0u;
    bool hasResult = false;
    std::array<BMMQ::IR::Operand, kMaxOperands> operands{};
};

struct InstructionPlan {
    std::uint8_t operationCount = 0u;
    std::uint8_t valueCount = 0u;
    bool hasTakenCondition = false;
    std::uint32_t takenCondition = 0u;
    std::array<PlannedOperation, kMaxOperations> operations{};
};

constexpr std::uint64_t mask(BMMQ::IR::ValueType type) noexcept
{
    using BMMQ::IR::ValueType;
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

constexpr unsigned bitWidth(BMMQ::IR::ValueType type) noexcept
{
    using BMMQ::IR::ValueType;
    switch (type) {
    case ValueType::Bool: return 1u;
    case ValueType::I8: return 8u;
    case ValueType::I16: return 16u;
    case ValueType::I32: return 32u;
    case ValueType::I64: return 64u;
    case ValueType::Void: return 0u;
    }
    return 0u;
}

constexpr std::uint64_t truncate(std::uint64_t value, BMMQ::IR::ValueType type) noexcept
{
    return value & mask(type);
}

std::uint32_t executePlan(const InstructionPlan* plan,
                          const IRExecution::ExecutionAbiV1* abi) noexcept
{
    if (plan == nullptr || abi == nullptr || !IRExecution::valid(*abi)) return 0u;
    std::array<std::uint64_t, kMaxValues> values{};
    bool branchTaken = false;
    bool exitRequested = false;
    bool retirementReached = false;

    const auto operandValue = [&](const BMMQ::IR::Operand& operand) noexcept {
        if (operand.kind == BMMQ::IR::OperandKind::Value) {
            const auto index = static_cast<std::size_t>(operand.payload);
            return index < values.size() ? values[index] : std::uint64_t{0};
        }
        return truncate(operand.payload, operand.type);
    };

    for (std::size_t operationIndex = 0u;
         operationIndex < plan->operationCount && !retirementReached;
         ++operationIndex) {
        const auto& operation = plan->operations[operationIndex];
        if (exitRequested && operation.opcode != BMMQ::IR::Opcode::RetireInstruction) continue;
        const auto lhs = [&]() noexcept { return operandValue(operation.operands[0]); };
        const auto rhs = [&]() noexcept { return operandValue(operation.operands[1]); };
        std::uint64_t value = 0u;
        using BMMQ::IR::Opcode;
        switch (operation.opcode) {
        case Opcode::Constant: value = lhs(); break;
        case Opcode::ReadRegister:
            value = abi->readRegister(abi->opaque,
                static_cast<IRExecution::Register>(operation.operands[0].payload));
            break;
        case Opcode::WriteRegister:
            abi->writeRegister(abi->opaque,
                static_cast<IRExecution::Register>(operation.operands[0].payload), rhs());
            break;
        case Opcode::LoadMemory:
            value = abi->readMemory8(abi->opaque, static_cast<std::uint16_t>(lhs()));
            break;
        case Opcode::StoreMemory:
            abi->writeMemory8(abi->opaque, static_cast<std::uint16_t>(lhs()),
                              static_cast<std::uint8_t>(rhs()));
            break;
        case Opcode::Add: value = lhs() + rhs(); break;
        case Opcode::Subtract: value = lhs() - rhs(); break;
        case Opcode::Multiply: value = lhs() * rhs(); break;
        case Opcode::BitAnd: value = lhs() & rhs(); break;
        case Opcode::BitOr: value = lhs() | rhs(); break;
        case Opcode::BitXor: value = lhs() ^ rhs(); break;
        case Opcode::ShiftLeft: {
            const auto width = bitWidth(operation.resultType);
            const auto shift = static_cast<unsigned>(rhs() & 63u);
            value = (width == 0u || shift >= width) ? 0u : truncate(lhs() << shift, operation.resultType);
            break;
        }
        case Opcode::ShiftRightLogical: {
            const auto width = bitWidth(operation.resultType);
            const auto shift = static_cast<unsigned>(rhs() & 63u);
            value = (width == 0u || shift >= width) ? 0u : truncate(lhs(), operation.resultType) >> shift;
            break;
        }
        case Opcode::ShiftRightArithmetic: {
            const auto width = bitWidth(operation.resultType);
            const auto shift = static_cast<unsigned>(rhs() & 63u);
            const auto source = truncate(lhs(), operation.resultType);
            if (width == 0u) value = 0u;
            else {
                const bool negative = (source & (std::uint64_t{1} << (width - 1u))) != 0u;
                if (shift >= width) value = negative ? mask(operation.resultType) : 0u;
                else if (shift == 0u) value = source;
                else {
                    value = source >> shift;
                    if (negative) value |= mask(operation.resultType) ^
                        ((std::uint64_t{1} << (width - shift)) - 1u);
                }
            }
            break;
        }
        case Opcode::BitNot: value = ~lhs(); break;
        case Opcode::CompareEqual: value = lhs() == rhs(); break;
        case Opcode::CompareNotEqual: value = lhs() != rhs(); break;
        case Opcode::CompareUnsignedLess: value = lhs() < rhs(); break;
        case Opcode::CompareSignedLess: {
            const auto width = bitWidth(operation.operands[0].type);
            const auto sign = width == 0u ? 0u : std::uint64_t{1} << (width - 1u);
            value = width != 0u && ((truncate(lhs(), operation.operands[0].type) ^ sign) <
                                   (truncate(rhs(), operation.operands[0].type) ^ sign));
            break;
        }
        case Opcode::Select: value = lhs() != 0u ? operandValue(operation.operands[1])
                                                 : operandValue(operation.operands[2]); break;
        case Opcode::SetProgramCounter:
            abi->writeRegister(abi->opaque, IRExecution::Register::PC, lhs()); break;
        case Opcode::Branch:
            abi->writeRegister(abi->opaque, IRExecution::Register::PC,
                               operation.operands[0].payload);
            branchTaken = true; break;
        case Opcode::BranchIf:
            if (lhs() != 0u) {
                abi->writeRegister(abi->opaque, IRExecution::Register::PC,
                                   operation.operands[1].payload);
                branchTaken = true;
            }
            break;
        case Opcode::CallHelper: {
            std::array<std::uint64_t, kMaxOperands - 1u> arguments{};
            for (std::size_t i = 1u; i < operation.operandCount; ++i) {
                arguments[i - 1u] = operandValue(operation.operands[i]);
            }
            value = abi->callHelper(abi->opaque,
                static_cast<IRExecution::Helper>(operation.operands[0].payload),
                arguments.data(), operation.operandCount - 1u);
            break;
        }
        case Opcode::Exit: exitRequested = true; break;
        case Opcode::RetireInstruction: retirementReached = true; break;
        }
        if (operation.hasResult) values[operation.result] = truncate(value, operation.resultType);
    }
    const bool cycleCondition = plan->hasTakenCondition &&
        plan->takenCondition < values.size() && values[plan->takenCondition] != 0u;
    return (branchTaken ? 1u : 0u) | (exitRequested ? 2u : 0u) |
           (cycleCondition ? 4u : 0u) | (retirementReached ? 8u : 0u);
}

bool buildPlan(const BMMQ::IR::GuestInstruction& instruction,
               InstructionPlan& plan, std::string& error)
{
    if (instruction.operations.size() > kMaxOperations) {
        error = "native instruction exceeds operation limit";
        return false;
    }
    std::size_t valueCount = 0u;
    for (std::size_t i = 0u; i < instruction.operations.size(); ++i) {
        const auto& source = instruction.operations[i];
        if (source.operands.size() > kMaxOperands) {
            error = "native operation exceeds operand limit";
            return false;
        }
        if ((source.opcode == BMMQ::IR::Opcode::LoadMemory &&
             source.resultType != BMMQ::IR::ValueType::I8) ||
            (source.opcode == BMMQ::IR::Opcode::StoreMemory &&
             source.operands[1].type != BMMQ::IR::ValueType::I8)) {
            error = "native Game Boy ABI supports only 8-bit memory operations";
            return false;
        }
        auto& destination = plan.operations[i];
        destination.opcode = source.opcode;
        destination.resultType = source.resultType;
        destination.memoryClass = source.memoryClass;
        destination.operandCount = static_cast<std::uint8_t>(source.operands.size());
        std::copy(source.operands.begin(), source.operands.end(), destination.operands.begin());
        if (source.result.has_value()) {
            if (*source.result >= kMaxValues) {
                error = "native instruction exceeds value limit";
                return false;
            }
            destination.hasResult = true;
            destination.result = *source.result;
            valueCount = std::max(valueCount, static_cast<std::size_t>(*source.result) + 1u);
        }
        for (const auto& operand : source.operands) {
            if (operand.kind == BMMQ::IR::OperandKind::Value && operand.payload >= kMaxValues) {
                error = "native operand exceeds value limit";
                return false;
            }
        }
    }
    if (instruction.takenCondition.has_value()) {
        if (*instruction.takenCondition >= kMaxValues) {
            error = "native taken condition exceeds value limit";
            return false;
        }
        plan.hasTakenCondition = true;
        plan.takenCondition = *instruction.takenCondition;
    }
    plan.operationCount = static_cast<std::uint8_t>(instruction.operations.size());
    plan.valueCount = static_cast<std::uint8_t>(valueCount);
    return true;
}

} // namespace

struct BlockArtifact::Impl {
    using Entry = std::uint32_t (*)(const IRExecution::ExecutionAbiV1*);
    std::vector<std::unique_ptr<InstructionPlan>> plans;
    std::vector<Entry> entries;
    void* mapping = nullptr;
    std::size_t mappingSize = 0u;
    std::size_t emittedBytes = 0u;
    bool sealed = false;
};

BlockArtifact::BlockArtifact(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

BlockArtifact::~BlockArtifact()
{
#if BMMQ_GAMEBOY_NATIVE_X64
    if (impl_ && impl_->mapping != nullptr) munmap(impl_->mapping, impl_->mappingSize);
#endif
}

bool supported() noexcept { return BMMQ_GAMEBOY_NATIVE_X64 != 0; }

std::shared_ptr<const BlockArtifact> compile(const BMMQ::IR::Block& block, std::string* error)
{
    const auto fail = [&](std::string message) -> std::shared_ptr<const BlockArtifact> {
        if (error != nullptr) *error = std::move(message);
        return {};
    };
    if (!supported()) return fail("native backend requires an x86-64 POSIX host");
    const auto validation = BMMQ::IR::validate(block);
    if (!validation) return fail("native backend requires validated IR: " + validation.message);
    if (block.instructions.empty() || block.instructions.size() > 16u) {
        return fail("native block instruction count is outside bounded limits");
    }

    auto impl = std::make_unique<BlockArtifact::Impl>();
    impl->plans.reserve(block.instructions.size());
    for (const auto& instruction : block.instructions) {
        auto plan = std::make_unique<InstructionPlan>();
        std::string planError;
        if (!buildPlan(instruction, *plan, planError)) return fail(std::move(planError));
        impl->plans.push_back(std::move(plan));
    }

#if BMMQ_GAMEBOY_NATIVE_X64
    const long pageSizeResult = sysconf(_SC_PAGESIZE);
    if (pageSizeResult <= 0) return fail("unable to query executable page size");
    const auto pageSize = static_cast<std::size_t>(pageSizeResult);
    const auto emittedBytes = impl->plans.size() * kThunkBytes;
    if (emittedBytes > std::numeric_limits<std::size_t>::max() - (pageSize - 1u)) {
        return fail("native code size overflow");
    }
    const auto mappingSize = ((emittedBytes + pageSize - 1u) / pageSize) * pageSize;
    void* mapping = mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return fail("unable to allocate writable native code page");
    impl->mapping = mapping;
    impl->mappingSize = mappingSize;
    impl->emittedBytes = emittedBytes;
    impl->entries.reserve(impl->plans.size());
    auto* output = static_cast<std::uint8_t*>(mapping);
    const auto executeAddress = reinterpret_cast<std::uintptr_t>(&executePlan);
    for (const auto& plan : impl->plans) {
        auto* thunk = output;
        *output++ = 0x48u; *output++ = 0x89u; *output++ = 0xFEu; // mov rsi,rdi
        *output++ = 0x48u; *output++ = 0xBFu;                    // movabs rdi,plan
        const auto planAddress = reinterpret_cast<std::uintptr_t>(plan.get());
        std::memcpy(output, &planAddress, sizeof(planAddress)); output += sizeof(planAddress);
        *output++ = 0x48u; *output++ = 0xB8u;                    // movabs rax,executor
        std::memcpy(output, &executeAddress, sizeof(executeAddress)); output += sizeof(executeAddress);
        *output++ = 0xFFu; *output++ = 0xE0u;                    // jmp rax
        impl->entries.push_back(reinterpret_cast<BlockArtifact::Impl::Entry>(thunk));
    }
    __builtin___clear_cache(static_cast<char*>(mapping), static_cast<char*>(mapping) + emittedBytes);
    if (mprotect(mapping, mappingSize, PROT_READ | PROT_EXEC) != 0) {
        munmap(mapping, mappingSize);
        impl->mapping = nullptr;
        return fail("unable to seal native code page read/execute");
    }
    impl->sealed = true;
#endif
    return std::shared_ptr<const BlockArtifact>(new BlockArtifact(std::move(impl)));
}

IRExecution::InstructionResult BlockArtifact::execute(
    std::size_t instructionIndex, const IRExecution::ExecutionAbiV1& abi) const
{
    if (!impl_ || !impl_->sealed || instructionIndex >= impl_->entries.size()) return {};
    const auto flags = impl_->entries[instructionIndex](&abi);
    return {
        .branchTaken = (flags & 1u) != 0u,
        .exitRequested = (flags & 2u) != 0u,
        .cycleCondition = (flags & 4u) != 0u,
        .retirementReached = (flags & 8u) != 0u,
    };
}

std::size_t BlockArtifact::instructionCount() const noexcept { return impl_ ? impl_->entries.size() : 0u; }
std::size_t BlockArtifact::codeSize() const noexcept { return impl_ ? impl_->emittedBytes : 0u; }
const void* BlockArtifact::codeAddress() const noexcept { return impl_ ? impl_->mapping : nullptr; }
bool BlockArtifact::sealedExecutable() const noexcept { return impl_ && impl_->sealed; }

} // namespace GB::NativeExecution
