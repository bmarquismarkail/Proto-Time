#include "DynamicPluginModule.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "inst_cycle/IrExecutionService.hpp"
#include "inst_cycle/IntermediateRepresentationInterpreter.hpp"
#include "machine/AudioService.hpp"
#include "machine/DebugSnapshotService.hpp"
#include "machine/Machine.hpp"
#include "machine/InputService.hpp"
#include "machine/TimingService.hpp"
#include "machine/VideoService.hpp"
#include "machine/plugins/AudioOutput.hpp"
#include "machine/plugins/abi/TimePluginAbi.h"

namespace BMMQ::Plugin {
namespace {

constexpr std::array kCppValueTypes{
    IR::ValueType::Void, IR::ValueType::Bool, IR::ValueType::I8,
    IR::ValueType::I16, IR::ValueType::I32, IR::ValueType::I64};
constexpr std::array<std::uint32_t, kCppValueTypes.size()> kCValueTypes{
    TIME_IR_VALUE_VOID_V1, TIME_IR_VALUE_BOOL_V1, TIME_IR_VALUE_I8_V1,
    TIME_IR_VALUE_I16_V1, TIME_IR_VALUE_I32_V1, TIME_IR_VALUE_I64_V1};
constexpr std::array kCppOperandKinds{
    IR::OperandKind::Value, IR::OperandKind::Immediate,
    IR::OperandKind::GuestRegister, IR::OperandKind::GuestAddress,
    IR::OperandKind::BlockTarget, IR::OperandKind::Helper};
constexpr std::array<std::uint32_t, kCppOperandKinds.size()> kCOperandKinds{
    TIME_IR_OPERAND_VALUE_V1, TIME_IR_OPERAND_IMMEDIATE_V1,
    TIME_IR_OPERAND_GUEST_REGISTER_V1, TIME_IR_OPERAND_GUEST_ADDRESS_V1,
    TIME_IR_OPERAND_BLOCK_TARGET_V1, TIME_IR_OPERAND_HELPER_V1};
constexpr std::array kCppOpcodes{
    IR::Opcode::Constant, IR::Opcode::ReadRegister, IR::Opcode::WriteRegister,
    IR::Opcode::LoadMemory, IR::Opcode::StoreMemory, IR::Opcode::Add,
    IR::Opcode::Subtract, IR::Opcode::Multiply, IR::Opcode::BitAnd,
    IR::Opcode::BitOr, IR::Opcode::BitXor, IR::Opcode::ShiftLeft,
    IR::Opcode::ShiftRightLogical, IR::Opcode::ShiftRightArithmetic,
    IR::Opcode::BitNot, IR::Opcode::CompareEqual, IR::Opcode::CompareNotEqual,
    IR::Opcode::CompareUnsignedLess, IR::Opcode::CompareSignedLess,
    IR::Opcode::Select, IR::Opcode::SetProgramCounter, IR::Opcode::Branch,
    IR::Opcode::BranchIf, IR::Opcode::CallHelper, IR::Opcode::Exit,
    IR::Opcode::RetireInstruction};
constexpr std::array<std::uint32_t, kCppOpcodes.size()> kCOpcodes{
    TIME_IR_OPCODE_CONSTANT_V1, TIME_IR_OPCODE_READ_REGISTER_V1,
    TIME_IR_OPCODE_WRITE_REGISTER_V1, TIME_IR_OPCODE_LOAD_MEMORY_V1,
    TIME_IR_OPCODE_STORE_MEMORY_V1, TIME_IR_OPCODE_ADD_V1,
    TIME_IR_OPCODE_SUBTRACT_V1, TIME_IR_OPCODE_MULTIPLY_V1,
    TIME_IR_OPCODE_BIT_AND_V1, TIME_IR_OPCODE_BIT_OR_V1,
    TIME_IR_OPCODE_BIT_XOR_V1, TIME_IR_OPCODE_SHIFT_LEFT_V1,
    TIME_IR_OPCODE_SHIFT_RIGHT_LOGICAL_V1,
    TIME_IR_OPCODE_SHIFT_RIGHT_ARITHMETIC_V1, TIME_IR_OPCODE_BIT_NOT_V1,
    TIME_IR_OPCODE_COMPARE_EQUAL_V1, TIME_IR_OPCODE_COMPARE_NOT_EQUAL_V1,
    TIME_IR_OPCODE_COMPARE_UNSIGNED_LESS_V1,
    TIME_IR_OPCODE_COMPARE_SIGNED_LESS_V1, TIME_IR_OPCODE_SELECT_V1,
    TIME_IR_OPCODE_SET_PROGRAM_COUNTER_V1, TIME_IR_OPCODE_BRANCH_V1,
    TIME_IR_OPCODE_BRANCH_IF_V1, TIME_IR_OPCODE_CALL_HELPER_V1,
    TIME_IR_OPCODE_EXIT_V1, TIME_IR_OPCODE_RETIRE_INSTRUCTION_V1};

template <typename Enum, std::size_t Size>
consteval bool enumValuesMatch(const std::array<Enum, Size>& cppValues,
                               const std::array<std::uint32_t, Size>& cValues)
{
    for (std::size_t index = 0u; index < Size; ++index) {
        if (static_cast<std::uint32_t>(cppValues[index]) != cValues[index]) return false;
    }
    return true;
}

static_assert(enumValuesMatch(kCppValueTypes, kCValueTypes));
static_assert(enumValuesMatch(kCppOperandKinds, kCOperandKinds));
static_assert(enumValuesMatch(kCppOpcodes, kCOpcodes));
static_assert(static_cast<std::uint32_t>(IR::MemoryClass::Generic) == TIME_IR_MEMORY_GENERIC_V1);
static_assert(static_cast<std::uint32_t>(IR::MemoryClass::Cartridge) == TIME_IR_MEMORY_CARTRIDGE_V1);
static_assert(static_cast<std::uint32_t>(IR::GuardKind::MappingGeneration) ==
              TIME_IR_GUARD_MAPPING_GENERATION_V1);
static_assert(static_cast<std::uint32_t>(IR::GuardKind::HelperAbi) == TIME_IR_GUARD_HELPER_ABI_V1);
static_assert(static_cast<std::uint32_t>(IR::BlockExit::Sequential) ==
              TIME_IR_BLOCK_EXIT_SEQUENTIAL_V1);
static_assert(static_cast<std::uint32_t>(IR::BlockExit::Unsupported) ==
              TIME_IR_BLOCK_EXIT_UNSUPPORTED_V1);

[[nodiscard]] ExecutionBackend mapBackend(std::uint32_t value)
{
    switch (value) {
    case TIME_EXECUTION_BACKEND_BASELINE_V1: return ExecutionBackend::Baseline;
    case TIME_EXECUTION_BACKEND_CACHED_BLOCK_V1: return ExecutionBackend::CachedBlock;
    case TIME_EXECUTION_BACKEND_PORTABLE_IR_V1: return ExecutionBackend::PortableIr;
    case TIME_EXECUTION_BACKEND_NATIVE_EXPERIMENTAL_V1: return ExecutionBackend::NativeExperimental;
    default: throw std::runtime_error("C executor policy returned invalid backend");
    }
}

[[nodiscard]] ExecutionGuarantee mapGuarantee(std::uint32_t value)
{
    switch (value) {
    case TIME_EXECUTION_GUARANTEE_BASELINE_FAITHFUL_V1: return ExecutionGuarantee::BaselineFaithful;
    case TIME_EXECUTION_GUARANTEE_VISIBLE_STATE_PRESERVING_V1: return ExecutionGuarantee::VisibleStatePreserving;
    case TIME_EXECUTION_GUARANTEE_EXPERIMENTAL_V1: return ExecutionGuarantee::Experimental;
    default: throw std::runtime_error("C executor policy returned invalid guarantee");
    }
}

[[nodiscard]] RuntimeCapabilityProfile mapCapabilities(std::uint32_t value)
{
    constexpr std::uint32_t known = TIME_RUNTIME_CAPABILITY_INTERCEPTION_V1 |
        TIME_RUNTIME_CAPABILITY_TRANSLATION_V1 |
        TIME_RUNTIME_CAPABILITY_INVALIDATION_V1 |
        TIME_RUNTIME_CAPABILITY_OPTIMIZATION_METADATA_V1;
    if ((value & ~known) != 0u) {
        throw std::runtime_error("C executor policy returned unknown capability bits");
    }
    return {
        (value & TIME_RUNTIME_CAPABILITY_INTERCEPTION_V1) != 0u,
        (value & TIME_RUNTIME_CAPABILITY_TRANSLATION_V1) != 0u,
        (value & TIME_RUNTIME_CAPABILITY_INVALIDATION_V1) != 0u,
        (value & TIME_RUNTIME_CAPABILITY_OPTIMIZATION_METADATA_V1) != 0u,
    };
}

[[nodiscard]] TimeExecutionObservationV1 makeObservation(
    const FetchBlock& block, const CpuFeedback& feedback) noexcept
{
    std::uint32_t flags = 0u;
    if (feedback.isControlFlow) flags |= TIME_EXECUTION_OBSERVATION_CONTROL_FLOW_V1;
    if (feedback.segmentBoundaryHint) flags |= TIME_EXECUTION_OBSERVATION_SEGMENT_BOUNDARY_V1;
    return TimeExecutionObservationV1{
        sizeof(TimeExecutionObservationV1), block.getbaseAddress(),
        static_cast<std::uint16_t>(feedback.pcBefore),
        static_cast<std::uint16_t>(feedback.pcAfter), feedback.retiredCycles, flags};
}

} // namespace

struct DynamicPluginModule::State {
    struct ExecutorEntry {
        std::string id;
        std::string displayName;
        const TimeExecutorPolicyApiV1* api = nullptr;
    };

    struct FrontendEntry {
        std::string id;
        std::string displayName;
        const TimeFrontendApiV1* api = nullptr;
    };

    struct AudioOutputEntry {
        std::string id;
        std::string displayName;
        const TimeAudioOutputApiV1* api = nullptr;
    };

    struct AudioProcessorEntry {
        std::string id;
        std::string displayName;
        const TimeAudioProcessorApiV1* api = nullptr;
    };

    struct IrExecutionBackendEntry {
        std::string id;
        std::string displayName;
        const TimeIrExecutionBackendApiV1* api = nullptr;
    };

    struct IrCoreAdapterEntry {
        std::string id;
        std::string displayName;
        const TimeIrCoreAdapterApiV1* api = nullptr;
    };

    ~State() {
        if (handle != nullptr) dlclose(handle);
    }

    void* handle = nullptr;
    std::string moduleId;
    std::string moduleDisplayName;
    std::vector<ExecutorEntry> executors;
    std::vector<FrontendEntry> frontends;
    std::vector<AudioOutputEntry> audioOutputs;
    std::vector<AudioProcessorEntry> audioProcessors;
    std::vector<IrCoreAdapterEntry> irAdapters;
    std::vector<IrExecutionBackendEntry> irBackends;
};

namespace {

class CAudioProcessorAdapter final : public IAudioProcessor {
public:
    CAudioProcessorAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                           const DynamicPluginModule::State::AudioProcessorEntry& entry,
                           std::uint32_t sampleRate, std::uint8_t channels,
                           std::size_t maxBlockSamples, std::string configJson)
        : state_(std::move(state)), api_(entry.api), configJson_(std::move(configJson))
    {
        try { instance_ = api_->create(&hostApi()); }
        catch (...) { throw std::runtime_error("C audio processor factory threw across the ABI"); }
        if (instance_ == nullptr) throw std::runtime_error("C audio processor factory returned null");
        const TimeAudioProcessorConfigV1 config{
            sizeof(TimeAudioProcessorConfigV1), sampleRate, channels,
            static_cast<std::uint32_t>(std::min<std::size_t>(maxBlockSamples, UINT32_MAX)),
            configJson_.empty() ? nullptr : configJson_.c_str()};
        bool opened = false;
        try { opened = api_->open(instance_, &config) != 0; }
        catch (...) {
            try { api_->destroy(instance_); } catch (...) {}
            instance_ = nullptr;
            throw std::runtime_error("C audio processor open threw across the ABI");
        }
        if (!opened) {
            try { api_->destroy(instance_); } catch (...) {}
            instance_ = nullptr;
            throw std::runtime_error("C audio processor open failed");
        }
        voices_.reserve(8u);
        events_.reserve(64u);
    }

    ~CAudioProcessorAdapter() override
    {
        if (instance_ != nullptr) {
            try { api_->close(instance_); } catch (...) {}
            try { api_->destroy(instance_); } catch (...) {}
        }
    }

    AudioProcessorCapabilities capabilities() const noexcept override { return {true, true}; }

    bool process(AudioBufferView input, std::span<int16_t> output,
                 std::size_t& producedSamples) noexcept override
    {
        AudioSourceBlockView block{.mixed = input, .voices = {}, .voiceStems = {}, .events = {}};
        return processSource(block, output, producedSamples);
    }

    bool processSource(const AudioSourceBlockView& input, std::span<int16_t> output,
                       std::size_t& producedSamples) noexcept override
    {
        producedSamples = 0u;
        if (disabled_) {
            const auto count = std::min(input.mixed.samples.size(), output.size());
            std::copy_n(input.mixed.samples.begin(), static_cast<std::ptrdiff_t>(count), output.begin());
            producedSamples = count;
            return true;
        }
        voices_.clear();
        for (const auto& voice : input.voices) {
            voices_.push_back(TimePsgVoiceV1{sizeof(TimePsgVoiceV1), voice.voiceId,
                                             static_cast<std::uint8_t>(voice.kind)});
        }
        events_.clear();
        for (const auto& event : input.events) {
            events_.push_back(TimePsgEventV1{
                sizeof(TimePsgEventV1), event.sampleFrameOffset, event.sequence,
                event.frequencyMilliHz, event.levelQ15, event.rawAddress,
                event.voiceId, static_cast<std::uint8_t>(event.voiceKind),
                static_cast<std::uint8_t>(event.kind), event.routingMask, event.timbre,
                event.rawValue, static_cast<std::uint8_t>(event.gate),
                static_cast<std::uint8_t>(event.hasRawWrite)});
        }
        const TimeAudioSourceBlockV1 block{
            sizeof(TimeAudioSourceBlockV1), static_cast<std::uint32_t>(input.mixed.sampleRate),
            input.mixed.channelCount, input.frameCounter, input.firstSampleFrame,
            input.lifecycleEpoch, input.mixed.samples.data(),
            static_cast<std::uint32_t>(input.mixed.samples.size()), input.voiceStems.data(),
            static_cast<std::uint32_t>(input.voiceStems.size()), voices_.data(),
            static_cast<std::uint32_t>(voices_.size()), events_.data(),
            static_cast<std::uint32_t>(events_.size())};
        std::uint32_t produced = 0u;
        int32_t result = TIME_AUDIO_PROCESSOR_ERROR_V1;
        try {
            result = api_->process(instance_, &block, output.data(),
                                   static_cast<std::uint32_t>(output.size()), &produced);
        } catch (...) {
            result = TIME_AUDIO_PROCESSOR_ERROR_V1;
        }
        if (result == TIME_AUDIO_PROCESSOR_BYPASS_V1) {
            const auto count = std::min(input.mixed.samples.size(), output.size());
            std::copy_n(input.mixed.samples.begin(), static_cast<std::ptrdiff_t>(count), output.begin());
            producedSamples = count;
            return true;
        }
        if (result != TIME_AUDIO_PROCESSOR_PROCESSED_V1 || produced != input.mixed.samples.size()) {
            captureLastError();
            disabled_ = true;
            const auto count = std::min(input.mixed.samples.size(), output.size());
            std::copy_n(input.mixed.samples.begin(), static_cast<std::ptrdiff_t>(count), output.begin());
            producedSamples = count;
            return true;
        }
        producedSamples = produced;
        return true;
    }

    void flush(std::uint64_t epoch) noexcept override
    {
        if (instance_ != nullptr) {
            try { api_->flush(instance_, epoch); } catch (...) { disabled_ = true; }
        }
    }

    [[nodiscard]] std::string lastError() const override
    {
        std::lock_guard<std::mutex> lock(diagnosticMutex_);
        return lastError_;
    }

private:
    void captureLastError() noexcept
    {
        try {
            const char* error = api_->last_error(instance_);
            std::lock_guard<std::mutex> lock(diagnosticMutex_);
            lastError_ = error != nullptr ? error : "C audio processor failed without an error message";
        } catch (...) {
            try {
                std::lock_guard<std::mutex> lock(diagnosticMutex_);
                lastError_ = "C audio processor last_error threw across the ABI";
            } catch (...) {}
        }
    }

    static const TimeHostApiV1& hostApi() noexcept
    {
        static const TimeHostApiV1 api{sizeof(TimeHostApiV1), TIME_PLUGIN_ABI_VERSION_V1,
                                       nullptr, nullptr};
        return api;
    }
    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeAudioProcessorApiV1* api_ = nullptr;
    void* instance_ = nullptr;
    std::string configJson_;
    std::vector<TimePsgVoiceV1> voices_;
    std::vector<TimePsgEventV1> events_;
    mutable std::mutex diagnosticMutex_;
    std::string lastError_;
    bool disabled_ = false;
};

[[nodiscard]] const TimeHostApiV1& irHostApi() noexcept
{
    static const TimeHostApiV1 api{sizeof(TimeHostApiV1), TIME_PLUGIN_ABI_VERSION_V1,
                                   nullptr, nullptr};
    return api;
}

[[nodiscard]] std::string cLastError(const char* (*lastError)(const void*),
                                     const void* instance,
                                     std::string fallback)
{
    try {
        const char* message = lastError(instance);
        if (message != nullptr && message[0] != '\0') return message;
    } catch (...) {
    }
    return fallback;
}

class IrBlockViewStorage {
public:
    explicit IrBlockViewStorage(const BMMQ::IR::Block& block)
    {
        guards_.reserve(block.guards.size());
        for (const auto& guard : block.guards) {
            guards_.push_back({sizeof(TimeIrGuardV1), static_cast<std::uint32_t>(guard.kind),
                               guard.subject, guard.expected, guard.mask,
                               static_cast<std::uint32_t>(guard.bytes.size()),
                               guard.bytes.empty() ? nullptr : guard.bytes.data()});
        }

        // Exposed TimeIrOperationV1.operands and TimeIrInstructionV1.operations
        // pointers remain valid only because every owning vector is fully
        // reserved before emplace_back stores them.
        operands_.reserve(block.instructions.size());
        operations_.reserve(block.instructions.size());
        instructions_.reserve(block.instructions.size());
#ifndef NDEBUG
        const auto operandsCapacity = operands_.capacity();
        const auto operationsCapacity = operations_.capacity();
        const auto instructionsCapacity = instructions_.capacity();
#endif
        for (const auto& instruction : block.instructions) {
            operands_.emplace_back();
            operations_.emplace_back();
            assert(operands_.capacity() == operandsCapacity);
            assert(operations_.capacity() == operationsCapacity);
            auto& instructionOperands = operands_.back();
            auto& instructionOperations = operations_.back();
            instructionOperands.reserve(instruction.operations.size());
            instructionOperations.reserve(instruction.operations.size());
#ifndef NDEBUG
            const auto instructionOperandsCapacity = instructionOperands.capacity();
            const auto instructionOperationsCapacity = instructionOperations.capacity();
#endif
            for (const auto& operation : instruction.operations) {
                instructionOperands.emplace_back();
                assert(instructionOperands.capacity() == instructionOperandsCapacity);
                auto& operationOperands = instructionOperands.back();
                operationOperands.reserve(operation.operands.size());
#ifndef NDEBUG
                const auto operationOperandsCapacity = operationOperands.capacity();
#endif
                for (const auto& operand : operation.operands) {
                    operationOperands.push_back({sizeof(TimeIrOperandV1),
                                                 static_cast<std::uint32_t>(operand.kind),
                                                 static_cast<std::uint32_t>(operand.type),
                                                 operand.payload});
                    assert(operationOperands.capacity() == operationOperandsCapacity);
                }
                instructionOperations.push_back({
                    sizeof(TimeIrOperationV1), static_cast<std::uint32_t>(operation.opcode),
                    operation.result.value_or(0u),
                    static_cast<std::uint32_t>(operation.resultType),
                    static_cast<std::uint32_t>(operation.memoryClass),
                    static_cast<std::uint32_t>(operationOperands.size()),
                    operationOperands.empty() ? nullptr : operationOperands.data()});
                assert(instructionOperations.capacity() == instructionOperationsCapacity);
            }
            std::uint32_t flags = 0u;
            if (instruction.controlFlow) flags |= TIME_IR_INSTRUCTION_CONTROL_FLOW_V1;
            if (instruction.interruptSensitive) {
                flags |= TIME_IR_INSTRUCTION_INTERRUPT_SENSITIVE_V1;
            }
            instructions_.push_back({
                sizeof(TimeIrInstructionV1), instruction.address, instruction.length,
                instruction.cyclesNotTaken, instruction.cyclesTaken,
                instruction.takenCondition.value_or(0u), flags,
                static_cast<std::uint32_t>(instructionOperations.size()),
                instructionOperations.empty() ? nullptr : instructionOperations.data()});
            assert(instructions_.capacity() == instructionsCapacity);
            assert(operands_.capacity() == operandsCapacity);
            assert(operations_.capacity() == operationsCapacity);
        }

        view_ = {sizeof(TimeIrBlockViewV1), TIME_IR_ABI_VERSION_V1,
                 block.guestStart, block.guestEnd, block.mappingGeneration,
                 static_cast<std::uint32_t>(block.exit),
                 static_cast<std::uint32_t>(guards_.size()),
                 guards_.empty() ? nullptr : guards_.data(),
                 static_cast<std::uint32_t>(instructions_.size()),
                 instructions_.empty() ? nullptr : instructions_.data()};
    }

    [[nodiscard]] const TimeIrBlockViewV1* get() const noexcept { return &view_; }

private:
    std::vector<TimeIrGuardV1> guards_{};
    std::vector<std::vector<std::vector<TimeIrOperandV1>>> operands_{};
    std::vector<std::vector<TimeIrOperationV1>> operations_{};
    std::vector<TimeIrInstructionV1> instructions_{};
    TimeIrBlockViewV1 view_{};
};

struct IrBuilderContext {
    BMMQ::IR::Block block{};
    bool blockBegun = false;
    bool instructionOpen = false;
    bool finished = false;
    bool failed = false;
    std::string error{};

    int fail(std::string message) noexcept
    {
        failed = true;
        try { error = std::move(message); } catch (...) {}
        return TIME_IR_ERROR_V1;
    }
};

template <typename Callback>
int32_t protectIrBuilderCallback(void* opaque, Callback&& callback) noexcept
{
    if (opaque == nullptr) return TIME_IR_ERROR_V1;
    auto& context = *static_cast<IrBuilderContext*>(opaque);
    try {
        return callback(context);
    } catch (...) {
        context.failed = true;
        try { context.error = "IR builder callback failed while copying plugin data"; }
        catch (...) {}
        return TIME_IR_ERROR_V1;
    }
}

[[nodiscard]] bool validValueType(std::uint32_t value) noexcept
{
    return value <= static_cast<std::uint32_t>(BMMQ::IR::ValueType::I64);
}

int32_t irBeginBlock(void* opaque, std::uint64_t guestStart,
                     std::uint64_t mappingGeneration) noexcept
{
    return protectIrBuilderCallback(opaque, [&](IrBuilderContext& context) -> int32_t {
        if (context.failed || context.blockBegun) return context.fail("invalid begin_block order");
        context.block = {};
        context.block.guestStart = guestStart;
        context.block.mappingGeneration = mappingGeneration;
        context.blockBegun = true;
        return TIME_IR_OK_V1;
    });
}

int32_t irAddGuard(void* opaque, const TimeIrGuardV1* guard) noexcept
{
    return protectIrBuilderCallback(opaque, [&](IrBuilderContext& context) -> int32_t {
        if (context.failed || !context.blockBegun || context.instructionOpen || context.finished ||
            guard == nullptr || guard->struct_size < sizeof(TimeIrGuardV1) ||
            guard->kind > static_cast<std::uint32_t>(BMMQ::IR::GuardKind::HelperAbi) ||
            context.block.guards.size() >= BMMQ::IR::IrExecutionService::Limits::kMaxGuards ||
            guard->byte_count > BMMQ::IR::IrExecutionService::Limits::kMaxGuestBytes ||
            (guard->byte_count != 0u && guard->bytes == nullptr)) {
            return context.fail("invalid IR guard supplied by plugin");
        }
        BMMQ::IR::Guard copied{.kind = static_cast<BMMQ::IR::GuardKind>(guard->kind),
                               .subject = guard->subject,
                               .expected = guard->expected,
                               .mask = guard->mask};
        if (guard->byte_count != 0u) {
            copied.bytes.assign(guard->bytes, guard->bytes + guard->byte_count);
        }
        context.block.guards.push_back(std::move(copied));
        return TIME_IR_OK_V1;
    });
}

int32_t irBeginInstruction(void* opaque, const TimeIrInstructionV1* instruction) noexcept
{
    return protectIrBuilderCallback(opaque, [&](IrBuilderContext& context) -> int32_t {
        constexpr std::uint32_t knownFlags = TIME_IR_INSTRUCTION_CONTROL_FLOW_V1 |
                                             TIME_IR_INSTRUCTION_INTERRUPT_SENSITIVE_V1;
        if (context.failed || !context.blockBegun || context.instructionOpen || context.finished ||
            instruction == nullptr || instruction->struct_size < sizeof(TimeIrInstructionV1) ||
            instruction->length == 0u || instruction->length > UINT8_MAX ||
            instruction->cycles_not_taken == 0u || instruction->cycles_taken == 0u ||
            (instruction->flags & ~knownFlags) != 0u ||
            context.block.instructions.size() >=
                BMMQ::IR::IrExecutionService::Limits::kMaxInstructions) {
            return context.fail("invalid IR instruction supplied by plugin");
        }
        context.block.instructions.push_back({
            .address = instruction->address,
            .length = static_cast<std::uint8_t>(instruction->length),
            .cyclesNotTaken = instruction->cycles_not_taken,
            .cyclesTaken = instruction->cycles_taken,
            .takenCondition = instruction->taken_condition == 0u
                                  ? std::nullopt
                                  : std::optional<BMMQ::IR::ValueId>(instruction->taken_condition),
            .controlFlow = (instruction->flags & TIME_IR_INSTRUCTION_CONTROL_FLOW_V1) != 0u,
            .interruptSensitive =
                (instruction->flags & TIME_IR_INSTRUCTION_INTERRUPT_SENSITIVE_V1) != 0u,
        });
        context.instructionOpen = true;
        return TIME_IR_OK_V1;
    });
}

int32_t irEmitOperation(void* opaque, const TimeIrOperationV1* operation) noexcept
{
    return protectIrBuilderCallback(opaque, [&](IrBuilderContext& context) -> int32_t {
        if (context.failed || !context.instructionOpen || operation == nullptr ||
            operation->struct_size < sizeof(TimeIrOperationV1) ||
            operation->opcode > static_cast<std::uint32_t>(BMMQ::IR::Opcode::Exit) ||
            !validValueType(operation->result_type) ||
            operation->memory_class > static_cast<std::uint32_t>(BMMQ::IR::MemoryClass::Cartridge) ||
            operation->operand_count > BMMQ::IR::IrExecutionService::Limits::kMaxOperandsPerOp ||
            (operation->operand_count != 0u && operation->operands == nullptr) ||
            operation->result_id > BMMQ::IR::IrExecutionService::Limits::kMaxValues ||
            context.block.instructions.back().operations.size() >=
                BMMQ::IR::IrExecutionService::Limits::kMaxOpsPerInstruction - 1u) {
            return context.fail("invalid IR operation supplied by plugin");
        }

        BMMQ::IR::Operation copied{
            .opcode = static_cast<BMMQ::IR::Opcode>(operation->opcode),
            .result = operation->result_id == 0u
                          ? std::nullopt
                          : std::optional<BMMQ::IR::ValueId>(operation->result_id),
            .resultType = static_cast<BMMQ::IR::ValueType>(operation->result_type),
            .memoryClass = static_cast<BMMQ::IR::MemoryClass>(operation->memory_class)};
        copied.operands.reserve(operation->operand_count);
        for (std::uint32_t index = 0u; index < operation->operand_count; ++index) {
            const auto& operand = operation->operands[index];
            if (operand.struct_size < sizeof(TimeIrOperandV1) ||
                operand.kind > static_cast<std::uint32_t>(BMMQ::IR::OperandKind::Helper) ||
                !validValueType(operand.type)) {
                return context.fail("invalid IR operand supplied by plugin");
            }
            copied.operands.push_back({.kind = static_cast<BMMQ::IR::OperandKind>(operand.kind),
                                       .type = static_cast<BMMQ::IR::ValueType>(operand.type),
                                       .payload = operand.payload});
        }
        context.block.instructions.back().operations.push_back(std::move(copied));
        return TIME_IR_OK_V1;
    });
}

int32_t irEndInstruction(void* opaque) noexcept
{
    return protectIrBuilderCallback(opaque, [](IrBuilderContext& context) -> int32_t {
        if (context.failed || !context.instructionOpen) {
            return context.fail("invalid end_instruction order");
        }
        context.block.instructions.back().operations.push_back(
            {.opcode = BMMQ::IR::Opcode::RetireInstruction});
        context.instructionOpen = false;
        return TIME_IR_OK_V1;
    });
}

int32_t irFinishBlock(void* opaque, std::uint32_t exitKind) noexcept
{
    return protectIrBuilderCallback(opaque, [&](IrBuilderContext& context) -> int32_t {
        if (context.failed || !context.blockBegun || context.instructionOpen || context.finished ||
            context.block.instructions.empty() ||
            exitKind > static_cast<std::uint32_t>(BMMQ::IR::BlockExit::Unsupported)) {
            return context.fail("invalid finish_block order or exit kind");
        }
        const auto& last = context.block.instructions.back();
        if (last.length > UINT64_MAX - last.address) {
            return context.fail("IR block end address overflows");
        }
        context.block.guestEnd = last.address + last.length - 1u;
        context.block.exit = static_cast<BMMQ::IR::BlockExit>(exitKind);
        context.finished = true;
        return TIME_IR_OK_V1;
    });
}

class CIrCoreAdapter final : public BMMQ::IR::IIrCoreAdapter {
public:
    CIrCoreAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                   const DynamicPluginModule::State::IrCoreAdapterEntry& entry)
        : state_(std::move(state)), api_(entry.api)
    {
        try { instance_ = api_->create(&irHostApi()); }
        catch (...) { throw std::runtime_error("C IR adapter factory threw across the ABI"); }
        if (instance_ == nullptr) throw std::runtime_error("C IR adapter factory returned null");
    }

    ~CIrCoreAdapter() override
    {
        if (instance_ != nullptr) {
            try { api_->destroy(instance_); } catch (...) {}
        }
    }

    std::uint32_t architectureId() const noexcept override { return api_->architecture_id; }
    std::uint32_t irAbiVersion() const noexcept override { return api_->ir_abi_version; }

    BMMQ::IR::BlockPtr lower(const BMMQ::IR::LoweringRequest& request,
                             std::string* error) override
    {
        if (request.instructions.empty() ||
            request.instructions.size() >
                BMMQ::IR::IrExecutionService::Limits::kMaxInstructions) {
            if (error != nullptr) *error = "invalid C IR lowering request";
            return {};
        }
        std::vector<TimeIrSourceInstructionV1> source;
        source.reserve(request.instructions.size());
        std::size_t guestBytes = 0u;
        for (const auto& instruction : request.instructions) {
            if (instruction.length == 0u || instruction.length > instruction.bytes.size()) {
                if (error != nullptr) *error = "invalid source instruction length";
                return {};
            }
            guestBytes += instruction.length;
            if (guestBytes > BMMQ::IR::IrExecutionService::Limits::kMaxGuestBytes) {
                if (error != nullptr) *error = "C IR lowering request exceeds byte limit";
                return {};
            }
            TimeIrSourceInstructionV1 copied{sizeof(TimeIrSourceInstructionV1),
                                             instruction.address, {}, instruction.length};
            std::copy(instruction.bytes.begin(), instruction.bytes.end(), copied.bytes);
            source.push_back(copied);
        }
        const TimeIrLoweringRequestV1 cRequest{
            sizeof(TimeIrLoweringRequestV1), TIME_IR_ABI_VERSION_V1,
            request.mappingGeneration, request.executionState,
            static_cast<std::uint32_t>(source.size()), source.data()};
        IrBuilderContext context;
        const TimeIrBuilderV1 builder{
            sizeof(TimeIrBuilderV1), TIME_IR_ABI_VERSION_V1, &context,
            &irBeginBlock, &irAddGuard, &irBeginInstruction, &irEmitOperation,
            &irEndInstruction, &irFinishBlock};

        int32_t result = TIME_IR_ERROR_V1;
        try { result = api_->lower(instance_, &cRequest, &builder); }
        catch (...) {
            if (error != nullptr) *error = "C IR adapter lower threw across the ABI";
            return {};
        }
        if (result != TIME_IR_OK_V1 || context.failed || !context.finished) {
            if (error != nullptr) {
                *error = context.error.empty()
                             ? cLastError(api_->last_error, instance_, "C IR adapter declined block")
                             : context.error;
            }
            return {};
        }
        const auto validation = BMMQ::IR::validate(context.block);
        if (!validation) {
            if (error != nullptr) *error = validation.message;
            return {};
        }
        return std::make_shared<const BMMQ::IR::Block>(std::move(context.block));
    }

    BMMQ::IR::ValidationResult validateBlock(const BMMQ::IR::Block& block) const override
    {
        IrBlockViewStorage storage(block);
        int32_t result = TIME_IR_ERROR_V1;
        try { result = api_->validate_block(instance_, storage.get()); }
        catch (...) {
            return {.valid = false, .message = "C IR adapter validation threw across the ABI"};
        }
        if (result == TIME_IR_OK_V1) return {};
        return {.valid = false,
                .message = cLastError(api_->last_error, instance_,
                                      "C IR adapter rejected block")};
    }

    std::optional<std::string> validateExecutionState(
        const BMMQ::IR::Block& block) const override
    {
        IrBlockViewStorage storage(block);
        int32_t result = TIME_IR_ERROR_V1;
        try { result = api_->validate_execution_state(instance_, storage.get()); }
        catch (...) { return "C IR adapter state validation threw across the ABI"; }
        if (result == TIME_IR_OK_V1) return {};
        return cLastError(api_->last_error, instance_, "C IR adapter state guard rejected");
    }

private:
    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeIrCoreAdapterApiV1* api_ = nullptr;
    void* instance_ = nullptr;
};

struct IrBackendLifetime {
    IrBackendLifetime(std::shared_ptr<DynamicPluginModule::State> owningState,
                      const TimeIrExecutionBackendApiV1* owningApi)
        : state(std::move(owningState)), api(owningApi)
    {
        try { instance = api->create(&irHostApi()); }
        catch (...) { throw std::runtime_error("C IR backend factory threw across the ABI"); }
        if (instance == nullptr) throw std::runtime_error("C IR backend factory returned null");
    }
    ~IrBackendLifetime()
    {
        if (instance != nullptr) {
            try { api->destroy(instance); } catch (...) {}
        }
    }
    std::shared_ptr<DynamicPluginModule::State> state;
    const TimeIrExecutionBackendApiV1* api = nullptr;
    void* instance = nullptr;
};

class CIrBackendArtifact final : public BMMQ::BlockBackendArtifact {
public:
    CIrBackendArtifact(std::shared_ptr<IrBackendLifetime> owningLifetime, void* artifact)
        : lifetime(std::move(owningLifetime)), raw(artifact) {}
    ~CIrBackendArtifact() override
    {
        if (raw != nullptr) {
            try { lifetime->api->destroy_artifact(lifetime->instance, raw); } catch (...) {}
        }
    }
    std::shared_ptr<IrBackendLifetime> lifetime;
    void* raw = nullptr;
};

class CIrExecutionBackend final : public BMMQ::IR::IIrExecutionBackend {
public:
    CIrExecutionBackend(std::shared_ptr<DynamicPluginModule::State> state,
                        const DynamicPluginModule::State::IrExecutionBackendEntry& entry)
        : lifetime_(std::make_shared<IrBackendLifetime>(std::move(state), entry.api)) {}

    bool supports(std::uint32_t architectureId,
                  std::uint32_t irAbiVersion) const noexcept override
    {
        return irAbiVersion == lifetime_->api->ir_abi_version &&
               (lifetime_->api->architecture_id == BMMQ::IR::kAnyArchitecture ||
                lifetime_->api->architecture_id == architectureId);
    }

    BMMQ::BlockBackendArtifactPtr compile(const BMMQ::IR::BlockPtr& block,
                                          std::string* error) override
    {
        if (!block) {
            if (error != nullptr) *error = "C IR backend received no block";
            return {};
        }
        IrBlockViewStorage storage(*block);
        void* raw = nullptr;
        try { raw = lifetime_->api->compile(lifetime_->instance, storage.get()); }
        catch (...) {
            if (error != nullptr) *error = "C IR backend compile threw across the ABI";
            return {};
        }
        if (raw == nullptr) {
            if (error != nullptr) {
                *error = cLastError(lifetime_->api->last_error, lifetime_->instance,
                                    "C IR backend declined block");
            }
            return {};
        }
        return std::make_shared<const CIrBackendArtifact>(lifetime_, raw);
    }

    bool execute(const BMMQ::BlockBackendArtifact& artifact,
                 std::size_t instructionIndex,
                 BMMQ::IR::InterpreterHost& host,
                 BMMQ::IR::InterpreterResult* result) override
    {
        const auto* cArtifact = dynamic_cast<const CIrBackendArtifact*>(&artifact);
        if (cArtifact == nullptr || cArtifact->lifetime.get() != lifetime_.get() ||
            instructionIndex > UINT32_MAX) return false;

        struct HostBridge {
            BMMQ::IR::InterpreterHost* host = nullptr;
            bool failed = false;
        } bridge{&host, false};
        const auto readRegister = +[](void* opaque, std::uint32_t id,
                                      std::uint32_t type) noexcept -> std::uint64_t {
            auto& bridge = *static_cast<HostBridge*>(opaque);
            try {
                if (type > static_cast<std::uint32_t>(BMMQ::IR::ValueType::I64)) throw 0;
                return bridge.host->readRegister(id, static_cast<BMMQ::IR::ValueType>(type));
            } catch (...) { bridge.failed = true; return 0u; }
        };
        const auto writeRegister = +[](void* opaque, std::uint32_t id, std::uint32_t type,
                                       std::uint64_t value) noexcept -> int32_t {
            auto& bridge = *static_cast<HostBridge*>(opaque);
            try {
                if (type > static_cast<std::uint32_t>(BMMQ::IR::ValueType::I64)) throw 0;
                bridge.host->writeRegister(id, static_cast<BMMQ::IR::ValueType>(type), value);
                return TIME_IR_OK_V1;
            } catch (...) { bridge.failed = true; return TIME_IR_ERROR_V1; }
        };
        const auto loadMemory = +[](void* opaque, std::uint64_t address, std::uint32_t type,
                                    std::uint32_t memoryClass) noexcept -> std::uint64_t {
            auto& bridge = *static_cast<HostBridge*>(opaque);
            try {
                if (type > static_cast<std::uint32_t>(BMMQ::IR::ValueType::I64) ||
                    memoryClass > static_cast<std::uint32_t>(BMMQ::IR::MemoryClass::Cartridge)) throw 0;
                return bridge.host->loadMemory(address, static_cast<BMMQ::IR::ValueType>(type),
                                               static_cast<BMMQ::IR::MemoryClass>(memoryClass));
            } catch (...) { bridge.failed = true; return 0u; }
        };
        const auto storeMemory = +[](void* opaque, std::uint64_t address, std::uint32_t type,
                                     std::uint32_t memoryClass,
                                     std::uint64_t value) noexcept -> int32_t {
            auto& bridge = *static_cast<HostBridge*>(opaque);
            try {
                if (type > static_cast<std::uint32_t>(BMMQ::IR::ValueType::I64) ||
                    memoryClass > static_cast<std::uint32_t>(BMMQ::IR::MemoryClass::Cartridge)) throw 0;
                bridge.host->storeMemory(address, static_cast<BMMQ::IR::ValueType>(type),
                                         static_cast<BMMQ::IR::MemoryClass>(memoryClass), value);
                return TIME_IR_OK_V1;
            } catch (...) { bridge.failed = true; return TIME_IR_ERROR_V1; }
        };
        const auto callHelper = +[](void* opaque, std::uint32_t id, std::uint32_t resultType,
                                    const std::uint64_t* arguments,
                                    std::uint32_t argumentCount) noexcept -> std::uint64_t {
            auto& bridge = *static_cast<HostBridge*>(opaque);
            try {
                if (resultType > static_cast<std::uint32_t>(BMMQ::IR::ValueType::I64) ||
                    argumentCount > BMMQ::IR::IrExecutionService::Limits::kMaxOperandsPerOp ||
                    (argumentCount != 0u && arguments == nullptr)) throw 0;
                return bridge.host->callHelper(
                    id, static_cast<BMMQ::IR::ValueType>(resultType),
                    std::span<const std::uint64_t>(arguments, argumentCount));
            } catch (...) { bridge.failed = true; return 0u; }
        };
        const auto setPc = +[](void* opaque, std::uint64_t address) noexcept -> int32_t {
            auto& bridge = *static_cast<HostBridge*>(opaque);
            try { bridge.host->setProgramCounter(address); return TIME_IR_OK_V1; }
            catch (...) { bridge.failed = true; return TIME_IR_ERROR_V1; }
        };
        const TimeIrExecutionHostV1 cHost{
            sizeof(TimeIrExecutionHostV1), TIME_IR_ABI_VERSION_V1, &bridge,
            readRegister, writeRegister, loadMemory, storeMemory, callHelper, setPc};
        TimeIrExecutionResultV1 cResult{sizeof(TimeIrExecutionResultV1), 0u, 0u, 0u, 0u};
        int32_t executed = TIME_IR_ERROR_V1;
        try {
            executed = lifetime_->api->execute(
                lifetime_->instance, cArtifact->raw, static_cast<std::uint32_t>(instructionIndex),
                &cHost, &cResult);
        } catch (...) { return false; }
        if (executed != TIME_IR_OK_V1 || bridge.failed ||
            cResult.struct_size < sizeof(TimeIrExecutionResultV1) ||
            cResult.branch_taken > 1u || cResult.exit_requested > 1u ||
            cResult.cycle_condition > 1u || cResult.retirement_reached > 1u) return false;
        if (result != nullptr) {
            *result = {.branchTaken = cResult.branch_taken != 0u,
                       .exitRequested = cResult.exit_requested != 0u,
                       .cycleCondition = cResult.cycle_condition != 0u,
                       .retirementReached = cResult.retirement_reached != 0u};
        }
        return true;
    }

private:
    std::shared_ptr<IrBackendLifetime> lifetime_;
};

class CExecutorPolicyAdapter final : public IExecutorPolicyPlugin {
public:
    CExecutorPolicyAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                           const DynamicPluginModule::State::ExecutorEntry& entry,
                           void* instance)
        : state_(std::move(state)), api_(entry.api), instance_(instance),
          metadata_{sizeof(PluginMetadata), entry.id, entry.displayName,
                    PluginKind::ExecutorPolicy, kHostAbiVersion}
    {
    }

    ~CExecutorPolicyAdapter() override {
        if (instance_ != nullptr) api_->destroy(instance_);
    }

    std::unique_ptr<IExecutorPolicyPlugin> clone() const override {
        void* instance = api_->create(&hostApi());
        if (instance == nullptr) throw std::runtime_error("C executor policy clone failed");
        DynamicPluginModule::State::ExecutorEntry entry{metadata_.id, metadata_.displayName, api_};
        try {
            return std::make_unique<CExecutorPolicyAdapter>(state_, entry, instance);
        } catch (...) {
            api_->destroy(instance);
            throw;
        }
    }

    const PluginMetadata& metadata() const override { return metadata_; }
    ExecutionBackend backend() const override { return mapBackend(api_->backend(instance_)); }
    ExecutionGuarantee guarantee() const override { return mapGuarantee(api_->guarantee(instance_)); }
    RuntimeCapabilityProfile requiredCapabilities() const override {
        return mapCapabilities(api_->required_capabilities(instance_));
    }
    bool shouldRecord(const FetchBlock& block, const CpuFeedback& feedback) const override {
        const auto observation = makeObservation(block, feedback);
        return api_->should_record(instance_, &observation) != 0;
    }
    bool shouldSegment(const FetchBlock& block, const CpuFeedback& feedback) const override {
        const auto observation = makeObservation(block, feedback);
        return api_->should_segment(instance_, &observation) != 0;
    }

    static const TimeHostApiV1& hostApi() noexcept {
        static const TimeHostApiV1 api{sizeof(TimeHostApiV1), TIME_PLUGIN_ABI_VERSION_V1,
                                       nullptr, nullptr};
        return api;
    }

private:
    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeExecutorPolicyApiV1* api_ = nullptr;
    void* instance_ = nullptr;
    PluginMetadata metadata_;
};

class CFrontendAdapter final : public IFrontendPlugin {
public:
    CFrontendAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                     const DynamicPluginModule::State::FrontendEntry& entry,
                     const FrontendConfig& config)
        : state_(std::move(state)), api_(entry.api), id_(entry.id),
          displayName_(entry.displayName), config_(config)
    {
        hostApi_ = TimeFrontendHostApiV1{
            sizeof(TimeFrontendHostApiV1), TIME_PLUGIN_ABI_VERSION_V1, this,
            &CFrontendAdapter::logMessage, &CFrontendAdapter::publishInput,
            &CFrontendAdapter::requestQuit, &CFrontendAdapter::requestControl};
        std::uint32_t flags = 0u;
        if (config_.enableVideo) flags |= TIME_FRONTEND_CONFIG_ENABLE_VIDEO_V1;
        if (config_.enableInput) flags |= TIME_FRONTEND_CONFIG_ENABLE_INPUT_V1;
        if (config_.createHiddenWindowOnInitialize) flags |= TIME_FRONTEND_CONFIG_CREATE_HIDDEN_V1;
        if (config_.showWindowOnPresent) flags |= TIME_FRONTEND_CONFIG_SHOW_ON_PRESENT_V1;
        const uint32_t clampedHdScale = std::clamp(config_.hdScale, 1u, 8u);
        const TimeFrontendConfigV1 cConfig{
            sizeof(TimeFrontendConfigV1), config_.windowTitle.c_str(), config_.windowScale,
            static_cast<int32_t>(std::max(config_.frameWidth, 1) * clampedHdScale),
            static_cast<int32_t>(std::max(config_.frameHeight, 1) * clampedHdScale),
            flags};
        try { instance_ = api_->create(&hostApi_, &cConfig); }
        catch (...) { throw std::runtime_error("C frontend factory threw across the ABI"); }
        if (instance_ == nullptr) throw std::runtime_error("C frontend factory returned null");
        presenter_ = std::make_unique<Presenter>(*this);
    }

    ~CFrontendAdapter() override
    {
        detachServices();
        if (instance_ != nullptr) {
            try { api_->shutdown(instance_); } catch (...) {}
            try { api_->destroy(instance_); } catch (...) {}
            instance_ = nullptr;
        }
    }

    std::string_view id() const override { return id_; }
    std::string_view displayName() const override { return displayName_; }
    InputPluginCapabilities capabilities() const noexcept override
    {
        return {.pollingSafe = true, .eventPumpSafe = true, .deterministic = true,
                .supportsDigital = true,
                .fixedLogicalLayout = true, .hotSwapSafe = true, .headlessSafe = false};
    }
    std::string_view name() const noexcept override { return backendName(); }
    bool open() override { return true; }
    void close() noexcept override {}
    std::string_view lastError() const noexcept override { return lastBackendError(); }

    void onAttach(MutableMachineView& view) override
    {
        ++stats_.attachCount;
        videoService_ = &view.videoService();
        timingService_ = &view.timingService();
        inputService_ = &view.inputService();
        if (config_.enableInput) {
            (void)inputService_->attachExternalAdapter(*this);
            (void)inputService_->configureMappingProfile("sdl-default");
            (void)inputService_->resume();
        }
        if (config_.enableVideo) {
            const uint32_t clampedHdScale = std::clamp(config_.hdScale, 1u, 8u);
            const bool configured = videoService_->configure({
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
                .mailboxDepthFrames = 1,
                .hdScale = static_cast<int>(clampedHdScale),
            });
            videoService_->setPresenterPolicy(config_.videoPresenterPolicy);
            const bool presenterConfigured = videoService_->configurePresenter({
                .windowTitle = config_.windowTitle,
                .scale = static_cast<int>(std::min<std::uint32_t>(
                    std::max(config_.windowScale, 1u),
                    static_cast<std::uint32_t>(std::numeric_limits<int>::max()))),
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
                .mode = VideoPresenterMode::Hardware,
                .createHiddenWindowOnOpen = config_.createHiddenWindowOnInitialize,
                .showWindowOnPresent = config_.showWindowOnPresent,
            });
            auto presenter = std::move(presenter_);
            const bool attached = configured && presenterConfigured &&
                videoService_->attachPresenter(std::move(presenter));
            const bool resumed = !config_.autoInitializeBackend ||
                (attached && videoService_->resume());
            if (!attached || !resumed) {
                diagnostics_.push_back("frontend: video service initialization failed");
            }
        } else if (config_.autoInitializeBackend) {
            (void)tryInitializeBackend();
        }
    }

    void onDetach(MutableMachineView&) override
    {
        ++stats_.detachCount;
        detachServices();
    }

    void onMachineEvent(const MachineEvent&, const MachineView&) override {}

    void onVideoEvent(const MachineEvent& event, const MachineView& view) override
    {
        videoEvents_.fetch_add(1u, std::memory_order_relaxed);
        if (!config_.enableVideo || videoService_ == nullptr) return;
        if (event.type == MachineEventType::RomLoaded) {
            (void)videoService_->submitRealtimeVideoPacket(event, {});
            return;
        }
        if (event.type != MachineEventType::VBlank) return;
        const auto started = std::chrono::steady_clock::now();
        auto submission = view.realtimeVideoPacket({
            .frameWidth = std::max(config_.frameWidth, 1),
            .frameHeight = std::max(config_.frameHeight, 1),
        });
        if (submission.has_value() &&
            videoService_->submitRealtimeVideoPacket(event, std::move(*submission))) {
            framesPrepared_.fetch_add(1u, std::memory_order_relaxed);
            videoPacketsAccepted_.fetch_add(1u, std::memory_order_relaxed);
            videoPacketsBuiltOutsideLock_.fetch_add(1u, std::memory_order_relaxed);
        } else {
            videoPacketsSkipped_.fetch_add(1u, std::memory_order_relaxed);
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        videoBuildLastNs_.store(elapsed, std::memory_order_relaxed);
        atomicMax(videoBuildHighWaterNs_, elapsed);
        videoBuildSamples_.fetch_add(1u, std::memory_order_relaxed);
    }

    std::optional<uint32_t> sampleDigitalInput(const MachineView&) override
    {
        if (!config_.enableInput) return std::nullopt;
        const auto value = inputMask_.load(std::memory_order_acquire);
        ++inputPolls_;
        inputSamplesProvided_.fetch_add(1u, std::memory_order_relaxed);
        return value >= 0 ? std::optional<uint32_t>(static_cast<uint32_t>(value))
                          : std::optional<uint32_t>(0u);
    }

    std::optional<InputButtonMask> sampleDigitalInput() override
    {
        if (!config_.enableInput) return std::nullopt;
        const auto value = inputMask_.load(std::memory_order_acquire);
        ++inputPolls_;
        inputSamplesProvided_.fetch_add(1u, std::memory_order_relaxed);
        return value >= 0
            ? std::optional<InputButtonMask>(static_cast<InputButtonMask>(value & 0xff))
            : std::optional<InputButtonMask>(static_cast<InputButtonMask>(0u));
    }

    void onDigitalInputEvent(const MachineEvent&, const MachineView& view) override
    {
        inputEvents_.fetch_add(1u, std::memory_order_relaxed);
        if (config_.retainDebugSnapshots || debugSnapshotService_ != nullptr) {
            lastInputState_ = view.digitalInputState();
        }
    }

    const FrontendConfig& config() const noexcept override { return config_; }

    FrontendStats stats() const noexcept override
    {
        auto result = stats_;
        result.videoEvents = videoEvents_.load(std::memory_order_relaxed);
        result.framesPrepared = framesPrepared_.load(std::memory_order_relaxed);
        result.videoRealtimePacketsAccepted = videoPacketsAccepted_.load(std::memory_order_relaxed);
        result.videoRealtimePacketsSkipped = videoPacketsSkipped_.load(std::memory_order_relaxed);
        result.videoRealtimePacketsBuiltOutsideLock =
            videoPacketsBuiltOutsideLock_.load(std::memory_order_relaxed);
        result.videoFrameBuildDurationLastNanos = videoBuildLastNs_.load(std::memory_order_relaxed);
        result.videoFrameBuildDurationHighWaterNanos =
            videoBuildHighWaterNs_.load(std::memory_order_relaxed);
        result.videoFrameBuildDurationSampleCount = videoBuildSamples_.load(std::memory_order_relaxed);
        result.inputEvents = inputEvents_.load(std::memory_order_relaxed);
        result.inputSamplesProvided = inputSamplesProvided_.load(std::memory_order_relaxed);
        result.inputPolls = inputPolls_.load(std::memory_order_relaxed);
        result.quitRequests = quitRequestCount_.load(std::memory_order_relaxed);
        TimeFrontendStatsV1 backend{};
        backend.struct_size = sizeof(TimeFrontendStatsV1);
        bool haveBackendStats = false;
        if (instance_ != nullptr) {
            try { haveBackendStats = api_->query_stats(instance_, &backend) != 0; }
            catch (...) { haveBackendStats = false; }
        }
        if (haveBackendStats) {
            result.renderAttempts = backend.frames_presented + backend.present_failures;
            result.framesPresented = backend.frames_presented;
            result.videoPresentCount = backend.frames_presented;
            result.videoPresenterTextureRecreateCount = backend.texture_recreate_count;
            result.videoPresenterTextureUploadCount = backend.texture_upload_count;
            result.videoPresenterRenderCount = backend.frames_presented;
            result.videoPresenterRendererFlags = backend.renderer_flags;
            result.videoPresenterPresentDurationLastNanos = backend.present_duration_last_ns;
            result.videoPresenterPresentDurationHighWaterNanos = backend.present_duration_high_water_ns;
            result.videoPresenterPresentDurationSampleCount = backend.frames_presented;
        }
        syncServiceStats(result);
        return result;
    }

    const std::vector<std::string>& diagnostics() const noexcept override { return diagnostics_; }
    const std::optional<VideoDebugFrameModel>& lastVideoDebugModel() const noexcept override {
        return lastVideoDebugModel_;
    }
    const std::optional<DigitalInputStateView>& lastInputState() const noexcept override {
        return lastInputState_;
    }
    const std::optional<FrontendFrameBuffer>& lastFrame() const noexcept override { return lastFrame_; }
    std::string_view lastRenderSummary() const noexcept override { return lastRenderSummary_; }
    bool windowVisible() const noexcept override { return backendStats().window_visible != 0; }
    bool windowVisibilityRequested() const noexcept override { return visibilityRequested_; }
    void requestWindowVisibility(bool visible) override
    {
        visibilityRequested_ = visible;
        if (instance_ != nullptr) {
            try { api_->set_window_visible(instance_, visible ? 1 : 0); } catch (...) {}
        }
    }

    bool serviceFrontend() override
    {
        if (instance_ == nullptr) return false;
        if (!backendReady() && !tryInitializeBackend()) return true;
        bool serviced = false;
        try { serviced = api_->service(instance_) != 0; } catch (...) { serviced = false; }
        if (videoService_ != nullptr && videoService_->hasPendingRealtimeFrame()) {
            (void)videoService_->presentOneFrame();
        }
        return serviced;
    }

    void setQueuedDigitalInputMask(uint32_t mask) override
    {
        inputMask_.store(static_cast<std::int32_t>(mask & 0xffu), std::memory_order_release);
    }
    void clearQueuedDigitalInputMask() override { inputMask_.store(-1, std::memory_order_release); }
    std::optional<uint32_t> queuedDigitalInputMask() const noexcept override
    {
        const auto value = inputMask_.load(std::memory_order_acquire);
        return value >= 0 ? std::optional<uint32_t>(static_cast<uint32_t>(value)) : std::nullopt;
    }
    void pressButton(InputButton button) override { setButton(button, true); }
    void releaseButton(InputButton button) override { setButton(button, false); }
    bool isButtonPressed(InputButton button) const noexcept override
    {
        const auto value = inputMask_.load(std::memory_order_acquire);
        return value >= 0 && (value & inputButtonMask(button)) != 0;
    }
    void clearQuitRequest() noexcept override { quitRequested_.store(false, std::memory_order_release); }
    bool quitRequested() const noexcept override { return quitRequested_.load(std::memory_order_acquire); }
    bool takeSaveStateRequest() noexcept override
    {
        return saveStateRequested_.exchange(false, std::memory_order_acq_rel);
    }
    std::string_view lastHostEventSummary() const noexcept override { return lastHostEventSummary_; }
    std::string_view lastBackendError() const noexcept override
    {
        refreshBackendStrings();
        return lastBackendError_;
    }
    std::string backendStatusSummary() const override
    {
        refreshBackendStrings();
        return backendReady() ? backendName_ + " ready" : backendName_ + ": " + lastBackendError_;
    }
    bool handleHostEvent(const FrontendHostEvent& event) override
    {
        lastHostEventSummary_ = "frontend host event";
        if (event.type == FrontendHostEventType::Quit) {
            requestQuit(&*this);
            return true;
        }
        const auto button = buttonForHostKey(event.key);
        if (button.has_value() && (event.type == FrontendHostEventType::KeyDown ||
                                  event.type == FrontendHostEventType::KeyUp)) {
            setButton(*button, event.type == FrontendHostEventType::KeyDown);
            return true;
        }
        if (event.type == FrontendHostEventType::KeyDown && !event.repeat) {
            handleControlKey(event.key);
            return event.key != FrontendHostKey::Unknown;
        }
        return false;
    }
    std::string_view backendName() const noexcept override
    {
        refreshBackendStrings();
        return backendName_;
    }
    bool backendReady() const noexcept override { return backendStats().backend_ready != 0; }
    bool tryInitializeBackend() override
    {
        if (instance_ == nullptr) return false;
        if (backendReady()) return true;
        ++stats_.backendInitAttempts;
        bool ok = false;
        try { ok = api_->initialize(instance_) != 0; } catch (...) { ok = false; }
        refreshBackendStrings();
        return ok;
    }
    std::size_t pumpBackendEvents() override
    {
        const auto before = backendStats().events_processed;
        if (instance_ != nullptr) {
            try { (void)api_->service(instance_); } catch (...) {}
        }
        const auto after = backendStats().events_processed;
        return static_cast<std::size_t>(after >= before ? after - before : 0u);
    }
    void setDebugSnapshotService(DebugSnapshotService* service) noexcept override { debugSnapshotService_ = service; }
    DebugSnapshotService* debugSnapshotService() const noexcept override { return debugSnapshotService_; }

private:
    template<typename T>
    static void atomicMax(std::atomic<T>& target, T value) noexcept
    {
        auto current = target.load(std::memory_order_relaxed);
        while (current < value &&
               !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
    }

    class Presenter final : public IVideoPresenterPlugin {
    public:
        explicit Presenter(CFrontendAdapter& owner) : owner_(owner) {}
        std::string_view name() const noexcept override { return owner_.backendName(); }
        VideoPluginCapabilities capabilities() const noexcept override {
            return {.realtimeSafe = true, .frameSizePreserving = true,
                    .requiresHostThreadAffinity = true};
        }
        bool open(const VideoPresenterConfig&) override { return owner_.tryInitializeBackend(); }
        void close() noexcept override {}
        bool ready() const noexcept override { return owner_.backendReady(); }
        bool present(const VideoFramePacket& frame) noexcept override
        {
            if (owner_.instance_ == nullptr || frame.pixels.empty()) return false;
            const TimeFrontendFrameV1 cFrame{
                sizeof(TimeFrontendFrameV1), TIME_FRONTEND_PIXEL_ARGB8888_V1,
                frame.width, frame.height,
                static_cast<std::uint32_t>(frame.width * static_cast<int>(sizeof(uint32_t))),
                frame.pixels.data(), frame.generation, frame.lifecycleEpoch};
            bool ok = false;
            try { ok = owner_.api_->present(owner_.instance_, &cFrame) != 0; } catch (...) { ok = false; }
            if (ok && owner_.config_.retainLastPresentedFrame) {
                FrontendFrameBuffer snapshot;
                snapshot.width = frame.width;
                snapshot.height = frame.height;
                snapshot.generation = frame.generation;
                snapshot.pixels = frame.pixels;
                owner_.lastFrame_ = std::move(snapshot);
                owner_.lastRenderSummary_ = "frontend frame presented";
            }
            return ok;
        }
        std::string_view lastError() const noexcept override { return owner_.lastBackendError(); }
        VideoPresenterDiagnostics diagnostics() const noexcept override
        {
            const auto stats = owner_.backendStats();
            VideoPresenterDiagnostics result;
            result.presentCount = stats.frames_presented;
            result.textureRecreateCount = stats.texture_recreate_count;
            result.textureUploadCount = stats.texture_upload_count;
            result.rendererName = owner_.backendName();
            result.rendererFlags = stats.renderer_flags;
            result.rendererAccelerated =
                (stats.renderer_flags & TIME_FRONTEND_RENDERER_ACCELERATED_V1) != 0u;
            result.presenterPresentDurationLastNanos = stats.present_duration_last_ns;
            result.presenterPresentDurationHighWaterNanos = stats.present_duration_high_water_ns;
            result.presenterPresentDurationSampleCount = stats.frames_presented;
            return result;
        }
        bool windowVisible() const noexcept override { return owner_.windowVisible(); }
        void requestWindowVisibility(bool value) noexcept override { owner_.requestWindowVisibility(value); }
        bool windowVisibilityRequested() const noexcept override { return owner_.windowVisibilityRequested(); }
    private:
        CFrontendAdapter& owner_;
    };

    static void logMessage(void* context, std::uint32_t, const char* message) noexcept
    {
        if (context == nullptr || message == nullptr) return;
        try { static_cast<CFrontendAdapter*>(context)->diagnostics_.emplace_back(message); }
        catch (...) {}
    }
    static void publishInput(void* context, std::uint32_t mask)
    {
        if (context == nullptr) return;
        auto& self = *static_cast<CFrontendAdapter*>(context);
        self.inputMask_.store(static_cast<std::int32_t>(mask & 0xffu), std::memory_order_release);
    }
    static void requestQuit(void* context)
    {
        if (context == nullptr) return;
        auto& self = *static_cast<CFrontendAdapter*>(context);
        self.quitRequested_.store(true, std::memory_order_release);
        self.quitRequestCount_.fetch_add(1u, std::memory_order_relaxed);
    }
    static void requestControl(void* context, std::uint32_t action) noexcept
    {
        try {
            if (context != nullptr) static_cast<CFrontendAdapter*>(context)->handleControl(action);
        } catch (...) {}
    }

    void handleControl(std::uint32_t action)
    {
        if (action == TIME_FRONTEND_CONTROL_SAVE_STATE_V1) {
            saveStateRequested_.store(true, std::memory_order_release);
            return;
        }
        if (timingService_ == nullptr) return;
        const auto timing = timingService_->stats();
        switch (action) {
        case TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1: timingService_->setPaused(!timing.paused); break;
        case TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1: timingService_->setThrottled(!timing.throttled); break;
        case TIME_FRONTEND_CONTROL_SINGLE_STEP_V1: timingService_->requestSingleStep(); break;
        case TIME_FRONTEND_CONTROL_SPEED_UP_V1:
            timingService_->setSpeedMultiplier(std::min(timing.speedMultiplier * 2.0, 16.0)); break;
        case TIME_FRONTEND_CONTROL_SPEED_DOWN_V1:
            timingService_->setSpeedMultiplier(std::max(timing.speedMultiplier * 0.5, 0.125)); break;
        default: break;
        }
    }

    void handleControlKey(FrontendHostKey key)
    {
        if (key == FrontendHostKey::Pause) handleControl(TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1);
        else if (key == FrontendHostKey::ThrottleToggle) handleControl(TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1);
        else if (key == FrontendHostKey::SingleStep) handleControl(TIME_FRONTEND_CONTROL_SINGLE_STEP_V1);
        else if (key == FrontendHostKey::SpeedUp) handleControl(TIME_FRONTEND_CONTROL_SPEED_UP_V1);
        else if (key == FrontendHostKey::SpeedDown) handleControl(TIME_FRONTEND_CONTROL_SPEED_DOWN_V1);
        else if (key == FrontendHostKey::SaveState) handleControl(TIME_FRONTEND_CONTROL_SAVE_STATE_V1);
    }

    static std::optional<InputButton> buttonForHostKey(FrontendHostKey key)
    {
        switch (key) {
        case FrontendHostKey::Right: return InputButton::Right;
        case FrontendHostKey::Left: return InputButton::Left;
        case FrontendHostKey::Up: return InputButton::Up;
        case FrontendHostKey::Down: return InputButton::Down;
        case FrontendHostKey::Z: return InputButton::Button1;
        case FrontendHostKey::X: return InputButton::Button2;
        case FrontendHostKey::Backspace: return InputButton::Meta1;
        case FrontendHostKey::Return: return InputButton::Meta2;
        default: return std::nullopt;
        }
    }
    void setButton(InputButton button, bool pressed)
    {
        auto current = inputMask_.load(std::memory_order_acquire);
        if (current < 0) current = 0;
        const auto bit = static_cast<std::int32_t>(inputButtonMask(button));
        const auto next = pressed ? (current | bit) : (current & ~bit);
        inputMask_.store(next, std::memory_order_release);
        ++stats_.buttonTransitions;
    }

    void detachServices() noexcept
    {
        if (videoService_ != nullptr) {
            (void)videoService_->pause();
            (void)videoService_->detachPresenter();
        }
        if (instance_ != nullptr) {
            try { api_->shutdown(instance_); } catch (...) {}
        }
        videoService_ = nullptr;
        timingService_ = nullptr;
        if (inputService_ != nullptr) {
            (void)inputService_->detachAdapter(inputService_->currentGeneration() + 1u);
            inputService_ = nullptr;
        }
    }

    TimeFrontendStatsV1 backendStats() const noexcept
    {
        TimeFrontendStatsV1 result{};
        result.struct_size = sizeof(TimeFrontendStatsV1);
        if (instance_ != nullptr) {
            try { (void)api_->query_stats(instance_, &result); } catch (...) {}
        }
        return result;
    }
    void refreshBackendStrings() const noexcept
    {
        if (instance_ == nullptr) return;
        try {
            const char* name = api_->backend_name(instance_);
            const char* error = api_->last_error(instance_);
            backendName_ = name != nullptr ? name : "frontend";
            lastBackendError_ = error != nullptr ? error : "";
        } catch (...) {
            lastBackendError_ = "frontend callback threw across the C ABI";
        }
    }
    void syncServiceStats(FrontendStats& result) const noexcept
    {
        if (videoService_ != nullptr) {
            const auto video = videoService_->diagnostics();
            result.videoFramesPublished = video.publishedFrameCount;
            result.videoMailboxDepth = video.mailboxDepth;
            result.videoMailboxHighWaterFrames = video.mailboxHighWaterMark;
            result.videoMailboxStaleDropCount = video.staleFrameDropCount;
            result.videoMailboxOverwriteCount = video.overwriteCount;
            result.videoLastPublishedGeneration = video.lastPublishedGeneration;
            result.videoLastPresentedGeneration = video.lastPresentedGeneration;
            result.videoPresentFreshFrameCount = video.presentFromFreshFrameCount;
            result.videoPresentFallbackCount = video.presentFallbackCount;
            result.videoStaleEpochDropCount = video.staleEpochDropCount;
            result.videoLifecycleEpochBumpCount = video.lifecycleEpochBumpCount;
            result.videoLifecycleEpoch = video.lifecycleEpoch;
            result.videoFrameAgeLastNs = video.frameAgeLastNs;
            result.videoFrameAgeHighWaterNs = video.frameAgeHighWaterNs;
            result.videoPresenterRendererName = video.presenterRendererName;
            result.videoPresenterRendererAccelerated =
                video.presenterRendererAccelerated;
        }
    }

    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeFrontendApiV1* api_ = nullptr;
    void* instance_ = nullptr;
    std::string id_;
    std::string displayName_;
    FrontendConfig config_;
    TimeFrontendHostApiV1 hostApi_{};
    std::unique_ptr<Presenter> presenter_;
    VideoService* videoService_ = nullptr;
    TimingService* timingService_ = nullptr;
    InputService* inputService_ = nullptr;
    DebugSnapshotService* debugSnapshotService_ = nullptr;
    mutable FrontendStats stats_{};
    std::atomic<std::size_t> videoEvents_{0u};
    std::atomic<std::size_t> framesPrepared_{0u};
    std::atomic<std::size_t> videoPacketsAccepted_{0u};
    std::atomic<std::size_t> videoPacketsSkipped_{0u};
    std::atomic<std::size_t> videoPacketsBuiltOutsideLock_{0u};
    std::atomic<std::int64_t> videoBuildLastNs_{0};
    std::atomic<std::int64_t> videoBuildHighWaterNs_{0};
    std::atomic<std::size_t> videoBuildSamples_{0u};
    std::atomic<std::size_t> inputEvents_{0u};
    std::atomic<std::size_t> inputSamplesProvided_{0u};
    mutable std::atomic<std::size_t> inputPolls_{0u};
    std::atomic<std::int32_t> inputMask_{0};
    std::atomic<bool> quitRequested_{false};
    std::atomic<bool> saveStateRequested_{false};
    std::atomic<std::size_t> quitRequestCount_{0u};
    bool visibilityRequested_ = false;
    std::vector<std::string> diagnostics_;
    std::optional<VideoDebugFrameModel> lastVideoDebugModel_;
    std::optional<DigitalInputStateView> lastInputState_;
    std::optional<FrontendFrameBuffer> lastFrame_;
    std::string lastRenderSummary_;
    std::string lastHostEventSummary_;
    mutable std::string backendName_ = "frontend";
    mutable std::string lastBackendError_;
};

class CAudioOutputAdapter final : public IAudioOutputBackend {
public:
    CAudioOutputAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                        const DynamicPluginModule::State::AudioOutputEntry& entry)
        : state_(std::move(state)), api_(entry.api), id_(entry.id), displayName_(entry.displayName)
    {
        hostApi_ = TimeAudioOutputHostApiV1{
            sizeof(TimeAudioOutputHostApiV1), TIME_PLUGIN_ABI_VERSION_V1, this,
            &CAudioOutputAdapter::drainReadyAudio};
        try { instance_ = api_->create(&hostApi_); }
        catch (...) { throw std::runtime_error("C audio-output factory threw across the ABI"); }
        if (instance_ == nullptr) {
            throw std::runtime_error("C audio-output factory returned null");
        }
    }

    ~CAudioOutputAdapter() override
    {
        close();
        if (instance_ != nullptr) {
            try { api_->destroy(instance_); } catch (...) {}
            instance_ = nullptr;
        }
    }

    std::string_view name() const noexcept override
    {
        if (instance_ == nullptr) return displayName_;
        try {
            const char* value = api_->backend_name(instance_);
            return value != nullptr ? std::string_view(value) : std::string_view(displayName_);
        } catch (...) {
            return displayName_;
        }
    }

    bool open(AudioEngine& engine, const AudioOutputOpenConfig& config) override
    {
        close();
        lastError_.clear();
        lastErrorCode_ = AudioOutputErrorCode::None;
        if (instance_ == nullptr || config.audioService == nullptr ||
            config.channels < 1 || config.channels > 2 || config.requestedSampleRate <= 0 ||
            config.callbackChunkSamples == 0u ||
            config.callbackChunkSamples > std::numeric_limits<std::uint32_t>::max()) {
            setError(AudioOutputErrorCode::InvalidConfig, "Invalid audio-output configuration");
            return false;
        }

        service_ = config.audioService;
        engine_ = &engine;
        readyQueueChunks_ = std::clamp<std::size_t>(config.readyQueueChunks, 1u, 64u);
        service_->setBackendPausedOrClosed(true);
        const TimeAudioOutputConfigV1 cConfig{
            sizeof(TimeAudioOutputConfigV1),
            static_cast<std::uint32_t>(config.requestedSampleRate),
            static_cast<std::uint32_t>(config.channels),
            static_cast<std::uint32_t>(config.callbackChunkSamples)};
        TimeAudioOutputDeviceInfoV1 obtained{};
        obtained.struct_size = sizeof(TimeAudioOutputDeviceInfoV1);
        bool opened = false;
        try { opened = api_->open(instance_, &cConfig, &obtained) != 0; }
        catch (...) { opened = false; }
        if (!opened) {
            captureModuleError(AudioOutputErrorCode::DeviceOpenFailed,
                               "Audio-output module failed to open");
            service_->setBackendPausedOrClosed(true);
            engine_ = nullptr;
            service_ = nullptr;
            return false;
        }
        if (obtained.struct_size < sizeof(TimeAudioOutputDeviceInfoV1) ||
            obtained.sample_rate == 0u || obtained.channels == 0u || obtained.channels > 2u ||
            obtained.callback_samples == 0u || obtained.callback_samples > (1u << 24u)) {
            setError(AudioOutputErrorCode::UnsupportedConfig,
                     "Audio-output module returned an invalid device format");
            try { api_->close(instance_); } catch (...) {}
            service_->setBackendPausedOrClosed(true);
            engine_ = nullptr;
            service_ = nullptr;
            return false;
        }

        deviceInfo_.sampleRate = config.testForcedDeviceSampleRate > 0
            ? config.testForcedDeviceSampleRate : static_cast<int>(obtained.sample_rate);
        deviceInfo_.channels = static_cast<int>(obtained.channels);
        deviceInfo_.callbackChunkSamples = obtained.callback_samples;
        engine_->setDeviceSampleRate(deviceInfo_.sampleRate);
        const bool configured = service_->configureFixedCallbackCapacity(
                                    deviceInfo_.callbackChunkSamples) &&
            service_->configureOutputTransport({
                .deviceSampleRate = deviceInfo_.sampleRate,
                .channelCount = static_cast<std::uint8_t>(deviceInfo_.channels),
                .callbackChunkSamples = deviceInfo_.callbackChunkSamples,
                .readyQueueChunks = readyQueueChunks_,
            }) && service_->startOutputTransport();
        if (!configured) {
            setError(AudioOutputErrorCode::InvalidConfig,
                     "Host audio transport configuration failed");
            try { api_->close(instance_); } catch (...) {}
            service_->stopOutputTransport();
            service_->setBackendPausedOrClosed(true);
            engine_ = nullptr;
            service_ = nullptr;
            deviceInfo_ = {};
            return false;
        }
        opened_ = true;
        return true;
    }

    void service() noexcept override
    {
        if (!opened_ || instance_ == nullptr || service_ == nullptr) return;
        try { (void)api_->service(instance_); } catch (...) {}
        const bool primed = service_->primedForDrain();
        if (primed && !started_) {
            bool started = false;
            try { started = api_->start(instance_) != 0; } catch (...) { started = false; }
            if (started) {
                service_->setBackendDrainActive(true);
                started_ = true;
            } else {
                captureModuleError(AudioOutputErrorCode::RuntimeError,
                                   "Audio-output module failed to start");
                opened_ = false;
            }
        } else if (!primed && started_) {
            try { api_->pause(instance_); } catch (...) {}
            service_->setBackendDrainActive(false);
            started_ = false;
        }
    }

    void close() noexcept override
    {
        const bool wasOpen = opened_;
        if (instance_ != nullptr && wasOpen) {
            try { api_->pause(instance_); } catch (...) {}
        }
        if (service_ != nullptr) service_->setBackendDrainActive(false);
        opened_ = false;
        if (instance_ != nullptr && wasOpen) {
            try { api_->close(instance_); } catch (...) {}
        }
        if (service_ != nullptr) {
            service_->stopOutputTransport();
            service_->setBackendPausedOrClosed(true);
        }
        started_ = false;
        engine_ = nullptr;
        service_ = nullptr;
        deviceInfo_ = {};
    }

    bool ready() const noexcept override { return opened_; }
    std::string lastError() const noexcept override { return lastError_; }
    AudioOutputErrorCode lastErrorCode() const noexcept override { return lastErrorCode_; }
    AudioOutputDeviceInfo deviceInfo() const noexcept override { return deviceInfo_; }

private:
    static std::uint32_t drainReadyAudio(void* context, std::int16_t* output,
                                         std::uint32_t requestedSamples) noexcept
    {
        if (context == nullptr || output == nullptr || requestedSamples == 0u) return 0u;
        auto& self = *static_cast<CAudioOutputAdapter*>(context);
        if (self.service_ == nullptr || !self.opened_) {
            std::fill_n(output, requestedSamples, std::int16_t{0});
            return 0u;
        }
        const auto started = std::chrono::steady_clock::now();
        self.service_->drainReadyOutput(
            std::span<std::int16_t>(output, static_cast<std::size_t>(requestedSamples)));
        self.service_->noteDrainCallbackDuration(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started));
        return requestedSamples;
    }

    void setError(AudioOutputErrorCode code, std::string message) noexcept
    {
        lastErrorCode_ = code;
        lastError_ = std::move(message);
    }

    void captureModuleError(AudioOutputErrorCode code, std::string fallback) noexcept
    {
        try {
            const char* value = api_->last_error(instance_);
            setError(code, value != nullptr && value[0] != '\0' ? std::string(value)
                                                                 : std::move(fallback));
        } catch (...) {
            setError(code, std::move(fallback));
        }
    }

    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeAudioOutputApiV1* api_ = nullptr;
    void* instance_ = nullptr;
    std::string id_;
    std::string displayName_;
    TimeAudioOutputHostApiV1 hostApi_{};
    AudioEngine* engine_ = nullptr;
    AudioService* service_ = nullptr;
    AudioOutputDeviceInfo deviceInfo_{};
    std::size_t readyQueueChunks_ = 3u;
    bool opened_ = false;
    bool started_ = false;
    std::string lastError_;
    AudioOutputErrorCode lastErrorCode_ = AudioOutputErrorCode::None;
};

[[nodiscard]] std::runtime_error loadError(const std::filesystem::path& path,
                                           const std::string& detail)
{
    return std::runtime_error("Unable to load plugin module '" + path.string() + "': " + detail);
}

} // namespace

DynamicPluginModule DynamicPluginModule::load(const std::filesystem::path& path)
{
    auto state = std::make_shared<State>();
    state->handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (state->handle == nullptr) {
        const char* error = dlerror();
        throw loadError(path, error != nullptr ? error : "unknown dynamic-loader error");
    }
    dlerror();
    const auto symbol = dlsym(state->handle, TIME_PLUGIN_MODULE_ENTRYPOINT_V1);
    if (const char* error = dlerror(); error != nullptr) throw loadError(path, error);
    const auto getModule = reinterpret_cast<TimeGetPluginModuleV1Fn>(symbol);
    if (getModule == nullptr) throw loadError(path, "module entrypoint is null");
    const auto* module = getModule();
    if (module == nullptr || module->struct_size < sizeof(TimePluginModuleV1) ||
        module->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || module->module_id == nullptr ||
        module->module_id[0] == '\0' || module->display_name == nullptr ||
        module->plugin_count > 1024u ||
        (module->plugin_count != 0u && module->plugin_at == nullptr)) {
        throw loadError(path, "invalid module descriptor");
    }
    state->moduleId = module->module_id;
    state->moduleDisplayName = module->display_name;
    std::unordered_set<std::string> ids;
    for (std::uint32_t index = 0; index < module->plugin_count; ++index) {
        const auto* descriptor = module->plugin_at(index);
        if (descriptor == nullptr || descriptor->struct_size < sizeof(TimePluginDescriptorV1) ||
            descriptor->plugin_id == nullptr || descriptor->plugin_id[0] == '\0' ||
            descriptor->display_name == nullptr || descriptor->api == nullptr) {
            throw loadError(path, "invalid plugin descriptor");
        }
        if (!ids.emplace(descriptor->plugin_id).second) {
            throw loadError(path, "duplicate plugin id: " + std::string(descriptor->plugin_id));
        }
        if (descriptor->kind == TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1) {
            if (descriptor->api_size < sizeof(TimeExecutorPolicyApiV1)) {
                throw loadError(path, "executor policy API size mismatch");
            }
            const auto* api = static_cast<const TimeExecutorPolicyApiV1*>(descriptor->api);
            if (api->struct_size < sizeof(TimeExecutorPolicyApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || api->create == nullptr ||
                api->destroy == nullptr || api->backend == nullptr || api->guarantee == nullptr ||
                api->required_capabilities == nullptr || api->should_record == nullptr ||
                api->should_segment == nullptr) {
                throw loadError(path, "incomplete executor policy API");
            }
            state->executors.push_back(State::ExecutorEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else if (descriptor->kind == TIME_PLUGIN_KIND_FRONTEND_V1) {
            if (descriptor->api_size < sizeof(TimeFrontendApiV1)) {
                throw loadError(path, "frontend API size mismatch");
            }
            const auto* api = static_cast<const TimeFrontendApiV1*>(descriptor->api);
            constexpr std::uint32_t knownCapabilities = TIME_FRONTEND_CAPABILITY_VIDEO_V1 |
                TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1 | TIME_FRONTEND_CAPABILITY_WINDOW_V1;
            if (api->struct_size < sizeof(TimeFrontendApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 ||
                (api->capabilities & ~knownCapabilities) != 0u || api->create == nullptr ||
                api->destroy == nullptr || api->initialize == nullptr || api->shutdown == nullptr ||
                api->service == nullptr || api->present == nullptr ||
                api->set_window_visible == nullptr || api->backend_name == nullptr ||
                api->last_error == nullptr || api->query_stats == nullptr) {
                throw loadError(path, "incomplete frontend API");
            }
            state->frontends.push_back(State::FrontendEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else if (descriptor->kind == TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1) {
            if (descriptor->api_size < sizeof(TimeAudioOutputApiV1)) {
                throw loadError(path, "audio-output API size mismatch");
            }
            const auto* api = static_cast<const TimeAudioOutputApiV1*>(descriptor->api);
            if (api->struct_size < sizeof(TimeAudioOutputApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || api->create == nullptr ||
                api->destroy == nullptr || api->open == nullptr || api->start == nullptr ||
                api->pause == nullptr || api->service == nullptr || api->close == nullptr ||
                api->backend_name == nullptr || api->last_error == nullptr ||
                api->query_stats == nullptr) {
                throw loadError(path, "incomplete audio-output API");
            }
            state->audioOutputs.push_back(State::AudioOutputEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else if (descriptor->kind == TIME_PLUGIN_KIND_AUDIO_PROCESSOR_V1) {
            if (descriptor->api_size < sizeof(TimeAudioProcessorApiV1)) {
                throw loadError(path, "audio-processor API size mismatch");
            }
            const auto* api = static_cast<const TimeAudioProcessorApiV1*>(descriptor->api);
            if (api->struct_size < sizeof(TimeAudioProcessorApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || api->create == nullptr ||
                api->destroy == nullptr || api->open == nullptr || api->process == nullptr ||
                api->flush == nullptr || api->close == nullptr || api->last_error == nullptr ||
                api->query_stats == nullptr) {
                throw loadError(path, "incomplete audio-processor API");
            }
            state->audioProcessors.push_back(State::AudioProcessorEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else if (descriptor->kind == TIME_PLUGIN_KIND_IR_CORE_ADAPTER_V1) {
            if (descriptor->api_size < sizeof(TimeIrCoreAdapterApiV1)) {
                throw loadError(path, "ir-core-adapter API size mismatch");
            }
            const auto* api = static_cast<const TimeIrCoreAdapterApiV1*>(descriptor->api);
            if (api->struct_size < sizeof(TimeIrCoreAdapterApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || api->create == nullptr ||
                api->destroy == nullptr || api->lower == nullptr || api->validate_block == nullptr ||
                api->validate_execution_state == nullptr || api->last_error == nullptr ||
                api->ir_abi_version != TIME_IR_ABI_VERSION_V1 ||
                api->architecture_id == 0u) {
                throw loadError(path, "incomplete ir-core-adapter API");
            }
            state->irAdapters.push_back(State::IrCoreAdapterEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else if (descriptor->kind == TIME_PLUGIN_KIND_IR_EXECUTION_BACKEND_V1) {
            if (descriptor->api_size < sizeof(TimeIrExecutionBackendApiV1)) {
                throw loadError(path, "ir-execution-backend API size mismatch");
            }
            const auto* api = static_cast<const TimeIrExecutionBackendApiV1*>(descriptor->api);
            if (api->struct_size < sizeof(TimeIrExecutionBackendApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || api->create == nullptr ||
                api->destroy == nullptr || api->compile == nullptr || api->execute == nullptr ||
                api->destroy_artifact == nullptr || api->last_error == nullptr ||
                api->ir_abi_version != TIME_IR_ABI_VERSION_V1) {
                throw loadError(path, "incomplete ir-execution-backend API");
            }
            state->irBackends.push_back(State::IrExecutionBackendEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else {
            throw loadError(path, "unsupported plugin kind");
        }
    }
    return DynamicPluginModule(std::move(state));
}

std::string_view DynamicPluginModule::id() const noexcept
{
    return state_ != nullptr ? std::string_view(state_->moduleId) : std::string_view{};
}

std::string_view DynamicPluginModule::displayName() const noexcept
{
    return state_ != nullptr ? std::string_view(state_->moduleDisplayName) : std::string_view{};
}

std::vector<std::string> DynamicPluginModule::executorPolicyIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->executors.size());
    for (const auto& executor : state_->executors) result.push_back(executor.id);
    return result;
}

std::unique_ptr<IExecutorPolicyPlugin> DynamicPluginModule::createExecutorPolicy(
    std::string_view id) const
{
    if (state_ == nullptr) throw std::runtime_error("plugin module is empty");
    const auto found = std::find_if(state_->executors.begin(), state_->executors.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->executors.end()) {
        throw std::invalid_argument("executor policy not found in module: " + std::string(id));
    }
    void* instance = found->api->create(&CExecutorPolicyAdapter::hostApi());
    if (instance == nullptr) throw std::runtime_error("C executor policy factory returned null");
    std::unique_ptr<CExecutorPolicyAdapter> result;
    try {
        result = std::make_unique<CExecutorPolicyAdapter>(state_, *found, instance);
    } catch (...) {
        found->api->destroy(instance);
        throw;
    }
    validateExecutorPolicyStartup(*result);
    return result;
}

std::vector<std::string> DynamicPluginModule::frontendIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->frontends.size());
    for (const auto& frontend : state_->frontends) result.push_back(frontend.id);
    return result;
}

std::unique_ptr<IFrontendPlugin> DynamicPluginModule::createFrontend(
    std::string_view id, const FrontendConfig& config) const
{
    if (state_ == nullptr) throw std::runtime_error("plugin module is empty");
    const auto found = std::find_if(state_->frontends.begin(), state_->frontends.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->frontends.end()) {
        throw std::invalid_argument("frontend not found in module: " + std::string(id));
    }
    if (config.enableVideo &&
        (found->api->capabilities & TIME_FRONTEND_CAPABILITY_VIDEO_V1) == 0u) {
        throw std::invalid_argument("frontend does not provide video: " + std::string(id));
    }
    if (config.enableInput &&
        (found->api->capabilities & TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1) == 0u) {
        throw std::invalid_argument("frontend does not provide digital input: " + std::string(id));
    }
    return std::make_unique<CFrontendAdapter>(state_, *found, config);
}

std::vector<std::string> DynamicPluginModule::audioOutputIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->audioOutputs.size());
    for (const auto& output : state_->audioOutputs) result.push_back(output.id);
    return result;
}

std::unique_ptr<IAudioOutputBackend> DynamicPluginModule::createAudioOutput(
    std::string_view id) const
{
    if (state_ == nullptr) throw std::runtime_error("plugin module is empty");
    const auto found = std::find_if(state_->audioOutputs.begin(), state_->audioOutputs.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->audioOutputs.end()) {
        throw std::invalid_argument("audio output not found in module: " + std::string(id));
    }
    return std::make_unique<CAudioOutputAdapter>(state_, *found);
}

std::vector<std::string> DynamicPluginModule::audioProcessorIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->audioProcessors.size());
    for (const auto& processor : state_->audioProcessors) result.push_back(processor.id);
    return result;
}

std::unique_ptr<IAudioProcessor> DynamicPluginModule::createAudioProcessor(
    std::string_view id, std::uint32_t sampleRate, std::uint8_t channels,
    std::size_t maxBlockSamples, std::string configJson) const
{
    if (state_ == nullptr) throw std::runtime_error("plugin module is empty");
    const auto found = std::find_if(state_->audioProcessors.begin(), state_->audioProcessors.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->audioProcessors.end()) {
        throw std::invalid_argument("audio processor not found in module: " + std::string(id));
    }
    return std::make_unique<CAudioProcessorAdapter>(state_, *found, sampleRate, channels,
                                                     maxBlockSamples, std::move(configJson));
}

std::vector<std::string> DynamicPluginModule::irCoreAdapterIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->irAdapters.size());
    for (const auto& adapter : state_->irAdapters) result.push_back(adapter.id);
    return result;
}

std::unique_ptr<BMMQ::IR::IIrCoreAdapter> DynamicPluginModule::createIrCoreAdapter(
    std::string_view id, std::string* error) const
{
    if (state_ == nullptr) {
        if (error != nullptr) *error = "plugin module is empty";
        return {};
    }
    const auto found = std::find_if(state_->irAdapters.begin(), state_->irAdapters.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->irAdapters.end()) {
        if (error != nullptr) *error = "IR core adapter not found in module: " + std::string(id);
        return {};
    }
    try {
        return std::make_unique<CIrCoreAdapter>(state_, *found);
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return {};
    }
}

std::vector<std::string> DynamicPluginModule::irExecutionBackendIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->irBackends.size());
    for (const auto& backend : state_->irBackends) result.push_back(backend.id);
    return result;
}

std::unique_ptr<BMMQ::IR::IIrExecutionBackend> DynamicPluginModule::createIrExecutionBackend(
    std::string_view id, std::string* error) const
{
    if (state_ == nullptr) {
        if (error != nullptr) *error = "plugin module is empty";
        return {};
    }
    const auto found = std::find_if(state_->irBackends.begin(), state_->irBackends.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->irBackends.end()) {
        if (error != nullptr) *error = "IR execution backend not found in module: " + std::string(id);
        return {};
    }
    try {
        return std::make_unique<CIrExecutionBackend>(state_, *found);
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return {};
    }
}

} // namespace BMMQ::Plugin
