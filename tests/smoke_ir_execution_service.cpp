#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include "inst_cycle/IrExecutionService.hpp"

namespace {

using namespace BMMQ::IR;

struct FakeArtifact final : BMMQ::BlockBackendArtifact {};

class StubAdapter final : public IIrCoreAdapter {
public:
    std::uint32_t architectureId() const noexcept override { return architectureId_; }
    std::uint32_t irAbiVersion() const noexcept override { return abiVersion_; }
    BlockPtr lower(const LoweringRequest&, std::string*) override { return {}; }
    ValidationResult validateBlock(const Block&) const override
    {
        if (throwBlock_) throw std::runtime_error("adapter validation failed");
        if (rejectBlock_) return {.valid = false, .message = "adapter rejected block"};
        return {};
    }
    std::optional<std::string> validateExecutionState(const Block&) const override
    {
        if (throwState_) throw std::runtime_error("adapter state check failed");
        if (rejectState_) return "execution state changed";
        return {};
    }

    std::uint32_t architectureId_ = 0x47420001u;
    std::uint32_t abiVersion_ = kIrAbiVersion;
    bool rejectBlock_ = false;
    bool rejectState_ = false;
    bool throwBlock_ = false;
    bool throwState_ = false;
};

class StubBackend final : public IIrExecutionBackend {
public:
    bool supports(std::uint32_t architectureId,
                  std::uint32_t abiVersion) const noexcept override
    {
        return supports_ && architectureId == expectedArchitecture_ &&
               abiVersion == kIrAbiVersion;
    }
    BMMQ::BlockBackendArtifactPtr compile(const BlockPtr&, std::string* error) override
    {
        ++compileCalls_;
        if (throwCompile_) throw std::runtime_error("backend compile failed");
        if (declineCompile_) {
            if (error != nullptr) *error = "backend declined block";
            return {};
        }
        return std::make_shared<FakeArtifact>();
    }
    bool execute(const BMMQ::BlockBackendArtifact&,
                 std::size_t,
                 InterpreterHost& host,
                 InterpreterResult* result) override
    {
        ++executeCalls_;
        if (throwExecute_) throw std::runtime_error("backend execute failed");
        if (!executeSucceeds_) return false;
        host.setProgramCounter(0x1234u);
        if (result != nullptr) {
            result->branchTaken = true;
            result->retirementReached = retire_;
        }
        return true;
    }

    std::uint32_t expectedArchitecture_ = 0x47420001u;
    bool supports_ = true;
    bool declineCompile_ = false;
    bool throwCompile_ = false;
    bool throwExecute_ = false;
    bool executeSucceeds_ = true;
    bool retire_ = true;
    std::size_t compileCalls_ = 0u;
    std::size_t executeCalls_ = 0u;
};

struct MockHost final : InterpreterHost {
    std::uint64_t readRegister(std::uint32_t, ValueType) override { return 0u; }
    void writeRegister(std::uint32_t, ValueType, std::uint64_t) override {}
    std::uint64_t loadMemory(std::uint64_t, ValueType, MemoryClass) override { return 0u; }
    void storeMemory(std::uint64_t, ValueType, MemoryClass, std::uint64_t) override {}
    std::uint64_t callHelper(std::uint32_t, ValueType,
                             std::span<const std::uint64_t>) override { return 0u; }
    void setProgramCounter(std::uint64_t value) override { pc = value; }
    std::uint64_t pc = 0u;
};

BlockPtr makeBlock()
{
    BlockBuilder builder(0x1000u);
    builder.beginInstruction(0x1000u, 1u, 4u, true);
    builder.emit(Opcode::Branch, {Operand::blockTarget(0x1234u)});
    builder.endInstruction();
    return builder.finish(BlockExit::ControlFlow);
}

LoweringRequest makeRequest()
{
    static constexpr std::array source{
        SourceInstruction{.address = 0x1000u,
                          .bytes = {0x00u, 0u, 0u, 0u},
                          .length = 1u},
    };
    return {.instructions = source,
            .mappingGeneration = 0u,
            .executionState = 0u};
}

void testCompatibilityAndValidationPrecedeCompile()
{
    StubAdapter adapter;
    StubBackend backend;
    IrExecutionService service(adapter, backend, adapter);

    backend.supports_ = false;
    auto result = service.prepare(makeRequest(), makeBlock());
    assert(!result.prepared && result.fallback);
    assert(backend.compileCalls_ == 0u);

    backend.supports_ = true;
    adapter.rejectBlock_ = true;
    result = service.prepare(makeRequest(), makeBlock());
    assert(!result.prepared && result.fallback);
    assert(result.message == "adapter rejected block");
    assert(backend.compileCalls_ == 0u);
}

void testCompileDeclineFallsBackWithoutPreparedState()
{
    StubAdapter adapter;
    StubBackend backend;
    backend.declineCompile_ = true;
    IrExecutionService service(adapter, backend, adapter);
    std::string error;
    const auto result = service.prepare(makeRequest(), makeBlock(), &error);
    assert(!result.prepared && result.fallback);
    assert(error == "backend declined block");
    assert(!result.artifact);
}

void testPreparationExceptionsAreFallbacks()
{
    StubAdapter adapter;
    StubBackend backend;
    IrExecutionService service(adapter, backend, adapter);

    adapter.throwBlock_ = true;
    auto result = service.prepare(makeRequest(), makeBlock());
    assert(!result.prepared && result.fallback);
    assert(result.message == "IR preparation failed: adapter validation failed");

    adapter.throwBlock_ = false;
    backend.throwCompile_ = true;
    result = service.prepare(makeRequest(), makeBlock());
    assert(!result.prepared && result.fallback);
    assert(result.message == "IR preparation failed: backend compile failed");
}

void testUntrustedNumericBoundsRejectBeforeCompile()
{
    StubAdapter adapter;
    StubBackend backend;
    IrExecutionService service(adapter, backend, adapter);

    auto oversizedId = std::make_shared<Block>(*makeBlock());
    oversizedId->instructions.front().operations.front().operands.front().kind =
        OperandKind::GuestRegister;
    oversizedId->instructions.front().operations.front().operands.front().payload =
        std::uint64_t{UINT32_MAX} + 1u;
    auto result = service.prepare(makeRequest(), oversizedId);
    assert(!result.prepared && result.fallback);
    assert(backend.compileCalls_ == 0u);

    auto oversizedCycles = std::make_shared<Block>(*makeBlock());
    oversizedCycles->instructions.front().cyclesNotTaken =
        IrExecutionService::Limits::kMaxCyclesPerInstruction + 1u;
    oversizedCycles->instructions.front().cyclesTaken =
        oversizedCycles->instructions.front().cyclesNotTaken;
    result = service.prepare(makeRequest(), oversizedCycles);
    assert(!result.prepared && result.fallback);
    assert(backend.compileCalls_ == 0u);
}

void testExecutionRetiresExactlyOneInstruction()
{
    StubAdapter adapter;
    StubBackend backend;
    IrExecutionService service(adapter, backend, adapter);
    const auto block = makeBlock();
    const auto prepared = service.prepare(makeRequest(), block);
    assert(prepared.prepared && prepared.artifact);

    MockHost host;
    InterpreterResult result;
    assert(service.tryExecute(*block, *prepared.artifact, 0u, host, &result));
    assert(host.pc == 0x1234u);
    assert(result.branchTaken && result.retirementReached);
    assert(backend.executeCalls_ == 1u);
}

void testStateGuardRejectsBeforeBackendMutation()
{
    StubAdapter adapter;
    StubBackend backend;
    IrExecutionService service(adapter, backend, adapter);
    const auto block = makeBlock();
    const auto prepared = service.prepare(makeRequest(), block);
    assert(prepared.prepared && prepared.artifact);
    adapter.rejectState_ = true;

    MockHost host;
    assert(!service.tryExecute(*block, *prepared.artifact, 0u, host));
    assert(host.pc == 0u);
    assert(backend.executeCalls_ == 0u);

    adapter.rejectState_ = false;
    adapter.throwState_ = true;
    assert(!service.tryExecute(*block, *prepared.artifact, 0u, host));
    assert(host.pc == 0u);
    assert(backend.executeCalls_ == 0u);
}

void testPostDispatchFailureIsFatal()
{
    StubAdapter adapter;
    StubBackend backend;
    backend.executeSucceeds_ = false;
    IrExecutionService service(adapter, backend, adapter);
    const auto block = makeBlock();
    auto prepared = service.prepare(makeRequest(), block);
    assert(prepared.prepared && prepared.artifact);

    MockHost host;
    bool threw = false;
    try {
        (void)service.tryExecute(*block, *prepared.artifact, 0u, host);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    backend.executeSucceeds_ = true;
    backend.throwExecute_ = true;
    prepared = service.prepare(makeRequest(), block);
    assert(prepared.prepared && prepared.artifact);
    threw = false;
    try {
        (void)service.tryExecute(*block, *prepared.artifact, 0u, host);
    } catch (const std::runtime_error& exception) {
        threw = std::string_view(exception.what()).find("backend execute failed") !=
            std::string_view::npos;
    }
    assert(threw);

    backend.throwExecute_ = false;
    backend.retire_ = false;
    prepared = service.prepare(makeRequest(), block);
    assert(prepared.prepared && prepared.artifact);
    threw = false;
    try {
        (void)service.tryExecute(*block, *prepared.artifact, 0u, host);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
}

void testBuiltInPortableBackendExecutesValidatedIr()
{
    StubAdapter adapter;
    PortableIrExecutionBackend backend;
    IrExecutionService service(adapter, backend, adapter);
    const auto block = makeBlock();
    const auto prepared = service.prepare(makeRequest(), block);
    assert(prepared.prepared && prepared.artifact);

    MockHost host;
    InterpreterResult result;
    assert(service.tryExecute(*block, *prepared.artifact, 0u, host, &result));
    assert(host.pc == 0x1234u);
    assert(result.branchTaken && result.retirementReached);

    const auto differentBlock = makeBlock();
    bool mismatchedBlockRejected = false;
    try {
        (void)service.tryExecute(*differentBlock, *prepared.artifact, 0u, host);
    } catch (const std::logic_error&) {
        mismatchedBlockRejected = true;
    }
    assert(mismatchedBlockRejected);
}

} // namespace

int main()
{
    testCompatibilityAndValidationPrecedeCompile();
    testCompileDeclineFallsBackWithoutPreparedState();
    testPreparationExceptionsAreFallbacks();
    testUntrustedNumericBoundsRejectBeforeCompile();
    testExecutionRetiresExactlyOneInstruction();
    testStateGuardRejectsBeforeBackendMutation();
    testPostDispatchFailureIsFatal();
    testBuiltInPortableBackendExecutesValidatedIr();
    return 0;
}
