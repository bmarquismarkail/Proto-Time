#include "GameGearMachine.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/plugins/IoPlugin.hpp"
#include "machine/plugins/PluginManager.hpp"
#include "machine/BackgroundTaskService.hpp"
#include "machine/SaveState.hpp"
#include "Z80Interpreter.hpp"
#include "GameGearVDP.hpp"
#include "GameGearPSG.hpp"
#include "GameGearInput.hpp"
#include "GameGearMapperFactory.hpp"
#include "GameGearMapper.hpp"
#include "GameGearCartridge.hpp"
#include <memory>
#include "GameGearMemoryMap.hpp"
#include "GameGearSaveManager.hpp"
#include "GameGearIrExecution.hpp"

namespace BMMQ {

namespace {
constexpr std::size_t kMaxRomSize = 1024u * 1024u;
constexpr uint8_t kGameGearViewportY = 24u;
constexpr uint8_t kGameGearViewportHeight = 144u;
// Expose only true Game Gear memory regions and port-based IO descriptors.
// Remove Game Boy memory-mapped video/input descriptors so tools see port IO.
constexpr std::array<IoRegionDescriptor, 5> kIoRegions{{
    {PluginCategory::System, 0x0000u, 0x8000u, "ROM", true, false},
    {PluginCategory::System, 0xC000u, 0x2000u, "RAM", true, true},
    {PluginCategory::Audio, 0xFF10u, 0x0017u, "APU Registers", true, true},
    {PluginCategory::Video, 0x00BEu, 0x0002u, "VDP Ports (IO)", true, true},
    {PluginCategory::DigitalInput, 0x00DCu, 0x0002u, "Input Ports (0xDC/0xDD)", true, false},
}};

SaveStateChunk makeChunk(std::string name, std::vector<uint8_t> data)
{
    SaveStateChunk chunk;
    chunk.name = std::move(name);
    chunk.size = static_cast<uint32_t>(data.size());
    chunk.data = std::move(data);
    return chunk;
}

const SaveStateChunk& requireChunk(const SaveStateFile& state, std::string_view name)
{
    const auto found = std::find_if(state.chunks.begin(), state.chunks.end(), [name](const auto& chunk) {
        return chunk.name == name;
    });
    if (found == state.chunks.end()) {
        throw std::invalid_argument("save state is missing required chunk");
    }
    return *found;
}

void appendU64(std::vector<uint8_t>& out, uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
    }
}

uint64_t readU64(const std::vector<uint8_t>& bytes, std::size_t& pos)
{
    if (pos > bytes.size() || bytes.size() - pos < 8u) {
        throw std::invalid_argument("Game Gear machine metadata truncated");
    }
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(bytes[pos++]) << (i * 8);
    }
    return value;
}

std::vector<uint8_t> serializeMachineMeta(uint64_t stepCounter,
                                          uint64_t lastAudioFrameCounter,
                                          bool interruptRequested,
                                          const std::optional<uint32_t>& lastDigitalInputMask,
                                          uint64_t inputGeneration)
{
    std::vector<uint8_t> bytes;
    appendU64(bytes, stepCounter);
    appendU64(bytes, lastAudioFrameCounter);
    bytes.push_back(interruptRequested ? 1u : 0u);
    bytes.push_back(lastDigitalInputMask.has_value() ? 1u : 0u);
    if (lastDigitalInputMask.has_value()) {
        const auto value = *lastDigitalInputMask;
        bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
        bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
    }
    appendU64(bytes, inputGeneration);
    return bytes;
}

[[nodiscard]] bool romPathAllowsSaveBinding(const std::optional<std::filesystem::path>& path)
{
    if (!path.has_value()) {
        return false;
    }
    if (!path->has_filename()) {
        return false;
    }
    auto extension = path->extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension != ".sms";
}

class GameGearIrHost final : public IR::InterpreterHost {
public:
    GameGearIrHost(Z80Interpreter& cpu, GameGearMemoryMap& memoryMap)
        : cpu_(cpu), memoryMap_(memoryMap)
    {
    }

    std::uint64_t readRegister(std::uint32_t id, IR::ValueType) override
    {
        using GameGearIR::Register;
        switch (static_cast<Register>(id)) {
        case Register::A: return static_cast<std::uint8_t>(cpu_.AF >> 8u);
        case Register::F: return static_cast<std::uint8_t>(cpu_.AF);
        case Register::B: return static_cast<std::uint8_t>(cpu_.BC >> 8u);
        case Register::C: return static_cast<std::uint8_t>(cpu_.BC);
        case Register::D: return static_cast<std::uint8_t>(cpu_.DE >> 8u);
        case Register::E: return static_cast<std::uint8_t>(cpu_.DE);
        case Register::H: return static_cast<std::uint8_t>(cpu_.HL >> 8u);
        case Register::L: return static_cast<std::uint8_t>(cpu_.HL);
        case Register::AF: return cpu_.AF;
        case Register::BC: return cpu_.BC;
        case Register::DE: return cpu_.DE;
        case Register::HL: return cpu_.HL;
        case Register::SP: return cpu_.SP;
        case Register::PC: return cpu_.PC;
        }
        throw std::invalid_argument("unknown Game Gear IR register");
    }

    void writeRegister(std::uint32_t id, IR::ValueType type, std::uint64_t value) override
    {
        using GameGearIR::Register;
        const auto byte = static_cast<std::uint8_t>(value);
        const auto word = static_cast<std::uint16_t>(value);
        switch (static_cast<Register>(id)) {
        case Register::A:
            cpu_.AF = static_cast<std::uint16_t>((cpu_.AF & 0x00FFu) |
                                                (static_cast<std::uint16_t>(byte) << 8u));
            return;
        case Register::F: cpu_.AF = static_cast<std::uint16_t>((cpu_.AF & 0xFF00u) | byte); return;
        case Register::B: cpu_.BC = static_cast<std::uint16_t>((cpu_.BC & 0x00FFu) |
                                                               (static_cast<std::uint16_t>(byte) << 8u)); return;
        case Register::C: cpu_.BC = static_cast<std::uint16_t>((cpu_.BC & 0xFF00u) | byte); return;
        case Register::D: cpu_.DE = static_cast<std::uint16_t>((cpu_.DE & 0x00FFu) |
                                                               (static_cast<std::uint16_t>(byte) << 8u)); return;
        case Register::E: cpu_.DE = static_cast<std::uint16_t>((cpu_.DE & 0xFF00u) | byte); return;
        case Register::H: cpu_.HL = static_cast<std::uint16_t>((cpu_.HL & 0x00FFu) |
                                                               (static_cast<std::uint16_t>(byte) << 8u)); return;
        case Register::L: cpu_.HL = static_cast<std::uint16_t>((cpu_.HL & 0xFF00u) | byte); return;
        case Register::AF: cpu_.AF = word; return;
        case Register::BC: cpu_.BC = word; return;
        case Register::DE: cpu_.DE = word; return;
        case Register::HL: cpu_.HL = word; return;
        case Register::SP: cpu_.SP = word; return;
        case Register::PC: cpu_.PC = word; return;
        }
        (void)type;
        throw std::invalid_argument("unknown Game Gear IR register");
    }

    std::uint64_t loadMemory(std::uint64_t address, IR::ValueType type,
                             IR::MemoryClass) override
    {
        if (type != IR::ValueType::I8) {
            throw std::invalid_argument("Game Gear IR memory load is not I8");
        }
        return memoryMap_.read(static_cast<std::uint16_t>(address));
    }

    void storeMemory(std::uint64_t address, IR::ValueType type, IR::MemoryClass,
                     std::uint64_t value) override
    {
        if (type != IR::ValueType::I8) {
            throw std::invalid_argument("Game Gear IR memory store is not I8");
        }
        memoryMap_.write(static_cast<std::uint16_t>(address), static_cast<std::uint8_t>(value));
    }

    std::uint64_t callHelper(std::uint32_t id, IR::ValueType,
                             std::span<const std::uint64_t> arguments) override
    {
        using GameGearIR::Helper;
        switch (static_cast<Helper>(id)) {
        case Helper::IncrementRefresh:
            cpu_.incrementRefreshForIr();
            return 0u;
        case Helper::UpdateIncrementFlags:
            if (arguments.size() != 2u) break;
            cpu_.updateIncrementFlagsForIr(static_cast<std::uint8_t>(arguments[0]),
                                           static_cast<std::uint8_t>(arguments[1]));
            return 0u;
        case Helper::UpdateDecrementFlags:
            if (arguments.size() != 2u) break;
            cpu_.updateDecrementFlagsForIr(static_cast<std::uint8_t>(arguments[0]),
                                           static_cast<std::uint8_t>(arguments[1]));
            return 0u;
        case Helper::ExecuteAlu8:
            if (arguments.size() != 3u) break;
            if (static_cast<std::uint8_t>(arguments[1]) !=
                static_cast<std::uint8_t>(cpu_.AF >> 8u)) {
                throw std::invalid_argument(
                    "Game Gear IR accumulator operand does not match CPU AF state");
            }
            return cpu_.executeAluForIr(static_cast<std::uint8_t>(arguments[0]),
                                        static_cast<std::uint8_t>(arguments[2]));
        }
        throw std::invalid_argument("invalid Game Gear IR helper call");
    }

    void setProgramCounter(std::uint64_t address) override
    {
        cpu_.PC = static_cast<std::uint16_t>(address);
    }

private:
    Z80Interpreter& cpu_;
    GameGearMemoryMap& memoryMap_;
};

class GameGearRuntimeContext final : public RuntimeContext,
                                     public ITranslationCapability,
                                     public IInvalidationCapability {
public:
    GameGearRuntimeContext(Z80Interpreter& cpu,
                           GameGearMemoryMap& memoryMap,
                           bool& romLoaded,
                           Plugin::IExecutorPolicyPlugin*& activePolicy,
                           bool& interruptRequested,
                           GameGearVDP& vdp)
        : cpu_(cpu),
          memoryMap_(memoryMap),
          romLoaded_(romLoaded),
          activePolicy_(activePolicy),
          interruptRequested_(interruptRequested),
          vdp_(vdp),
          irAdapter_(this, &GameGearRuntimeContext::executionStateCallback),
          activeIrAdapter_(&irAdapter_),
          activeIrBackend_(&irBackend_),
          irService_(std::make_unique<IR::IrExecutionService>(*activeIrAdapter_,
                                                             *activeIrBackend_)),
          irHost_(cpu_, memoryMap_)
    {
    }

    FetchBlock fetch() override {
        FetchBlock block;
        block.setbaseAddress(cpu_.PC);
        auto& entries = block.getblockData();
        entries.push_back(fetchBlockData<AddressType, DataType>{cpu_.PC, {memoryMap_.read(cpu_.PC)}});
        return block;
    }

    ExecutionBlock decode(FetchBlock&) override {
        ExecutionBlock block;
        return block;
    }

    void execute(const ExecutionBlock&, FetchBlock&) override {
        step();
    }

    CpuFeedback step(FetchBlock&) override {
        return step();
    }

    CpuFeedback step() override {
        if (!romLoaded_) {
            return lastFeedback_;
        }
        lastFeedback_.pcBefore = cpu_.PC;
        if (activePolicy_->backend() == ExecutionBackend::PortableIr &&
            tryStepIr(lastFeedback_)) {
            return lastFeedback_;
        }
        const auto retiredCycles = cpu_.step();
        lastFeedback_.pcAfter = cpu_.PC;
        lastFeedback_.retiredCycles = retiredCycles;
        lastFeedback_.segmentBoundaryHint = false;
        lastFeedback_.isControlFlow = false;
        lastFeedback_.executionPath = ExecutionPathHint::CanonicalFetchDecodeExecute;
        return lastFeedback_;
    }

    DataType read8(AddressType address) const override {
        return memoryMap_.read(address);
    }

    void write8(AddressType address, DataType value) override {
        memoryMap_.write(address, value);
    }

    uint8_t readRegister8(std::string_view id) const override {
        if (id == "A") {
            return static_cast<uint8_t>((cpu_.AF >> 8) & 0x00FFu);
        }
        if (id == "F") {
            return static_cast<uint8_t>(cpu_.AF & 0x00FFu);
        }
        throw std::invalid_argument("register not found");
    }

    void writeRegister8(std::string_view id, uint8_t value) override {
        if (id == "A") {
            cpu_.AF = static_cast<uint16_t>((cpu_.AF & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
            return;
        }
        if (id == "F") {
            cpu_.AF = static_cast<uint16_t>((cpu_.AF & 0xFF00u) | value);
            return;
        }
        throw std::invalid_argument("register not found");
    }

    uint16_t readRegister16(std::string_view id) const override {
        return registerRefConst(id);
    }

    void writeRegister16(std::string_view id, uint16_t value) override {
        registerRef(id) = value;
    }

    const CpuFeedback& getLastFeedback() const override {
        return lastFeedback_;
    }

    uint32_t clockHz() const override {
        return 3579545u;
    }

    ExecutionGuarantee guarantee() const override {
        return activePolicy_->guarantee();
    }

    const Plugin::PluginMetadata* attachedPolicyMetadata() const override {
        return &activePolicy_->metadata();
    }

    const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {
        return *activePolicy_;
    }

    ITranslationCapability* translationCapability() override { return this; }
    IInvalidationCapability* invalidationCapability() override { return this; }
    const ITranslationCapability* translationCapability() const override { return this; }
    const IInvalidationCapability* invalidationCapability() const override { return this; }

    void clearIrCache() noexcept { irCache_.clear(); }
    [[nodiscard]] GameGearIrStats irStats() const noexcept { return irStats_; }
    void setIrComponents(std::unique_ptr<IR::IIrCoreAdapter> adapter,
                         std::unique_ptr<IR::IIrExecutionBackend> backend)
    {
        auto* selectedAdapter = adapter
            ? adapter.get()
            : static_cast<IR::IIrCoreAdapter*>(&irAdapter_);
        auto* selectedBackend = backend
            ? backend.get()
            : static_cast<IR::IIrExecutionBackend*>(&irBackend_);
        if (selectedAdapter->architectureId() != GameGearIR::kArchitectureId ||
            selectedAdapter->irAbiVersion() != IR::kIrAbiVersion ||
            !selectedBackend->supports(selectedAdapter->architectureId(),
                                       selectedAdapter->irAbiVersion())) {
            throw std::invalid_argument("incompatible Game Gear IR adapter/backend selection");
        }
        auto replacementService = std::make_unique<IR::IrExecutionService>(
            *selectedAdapter, *selectedBackend);
        clearIrCache();
        irService_ = std::move(replacementService);
        dynamicIrAdapter_ = std::move(adapter);
        dynamicIrBackend_ = std::move(backend);
        activeIrAdapter_ = dynamicIrAdapter_ ? dynamicIrAdapter_.get() : &irAdapter_;
        activeIrBackend_ = dynamicIrBackend_ ? dynamicIrBackend_.get() : &irBackend_;
    }

private:
    struct IrCacheEntry {
        std::uint64_t mappingGeneration = 0u;
        std::vector<std::uint8_t> codeBytes{};
        IR::BlockPtr block{};
        BlockBackendArtifactPtr artifact{};
    };

    static std::uint64_t executionStateCallback(const void* opaque) noexcept
    {
        return static_cast<const GameGearRuntimeContext*>(opaque)->executionState();
    }

    [[nodiscard]] std::uint64_t executionState() const noexcept
    {
        std::uint64_t state = 0u;
        if (cpu_.halted()) state |= GameGearIR::Halted;
        if (interruptRequested_ || vdp_.isIrqAsserted()) state |= GameGearIR::InterruptPending;
        if (cpu_.deferredInterruptEnable()) state |= GameGearIR::DeferredInterruptEnable;
        return state;
    }

    static std::optional<std::uint8_t> irLength(std::uint8_t opcode) noexcept
    {
        if (opcode == 0x00u || (opcode >= 0x40u && opcode <= 0xBFu && opcode != 0x76u) ||
            (opcode & 0xC7u) == 0x04u || (opcode & 0xC7u) == 0x05u) {
            return 1u;
        }
        if ((opcode & 0xC7u) == 0x06u || opcode == 0x18u) return 2u;
        return {};
    }

    bool guardsMatch(const IrCacheEntry& entry) const noexcept
    {
        if (entry.mappingGeneration != memoryMap_.codeMappingGeneration()) return false;
        for (std::size_t index = 0u; index < entry.codeBytes.size(); ++index) {
            std::uint8_t byte = 0u;
            if (!memoryMap_.peekCodeByte(static_cast<std::uint16_t>(cpu_.PC + index), byte) ||
                byte != entry.codeBytes[index]) {
                return false;
            }
        }
        return executionState() == 0u;
    }

    bool tryStepIr(CpuFeedback& feedback)
    {
        if (executionState() != 0u) {
            ++irStats_.fallbacks;
            return false;
        }

        std::uint8_t opcode = 0u;
        if (!memoryMap_.peekCodeByte(cpu_.PC, opcode)) {
            ++irStats_.fallbacks;
            return false;
        }
        const auto length = irLength(opcode);
        if (!length.has_value()) {
            ++irStats_.fallbacks;
            return false;
        }

        std::vector<std::uint8_t> bytes(*length);
        for (std::size_t index = 0u; index < bytes.size(); ++index) {
            if (!memoryMap_.peekCodeByte(static_cast<std::uint16_t>(cpu_.PC + index), bytes[index])) {
                ++irStats_.fallbacks;
                return false;
            }
        }

        auto found = irCache_.find(cpu_.PC);
        if (found == irCache_.end() || found->second.mappingGeneration !=
                                           memoryMap_.codeMappingGeneration() ||
            found->second.codeBytes != bytes) {
            IR::SourceInstruction source{.address = cpu_.PC, .length = *length};
            std::copy(bytes.begin(), bytes.end(), source.bytes.begin());
            const std::array sourceBlock{source};
            std::string error;
            IR::BlockPtr block;
            try {
                block = activeIrAdapter_->lower({.instructions = sourceBlock,
                                                 .mappingGeneration =
                                                     memoryMap_.codeMappingGeneration(),
                                                 .executionState = executionState()},
                                                &error);
            } catch (...) {
                ++irStats_.fallbacks;
                return false;
            }
            if (!block || block->instructions.size() != 1u ||
                block->instructions.front().address != cpu_.PC ||
                block->instructions.front().length != *length) {
                ++irStats_.fallbacks;
                return false;
            }
            const auto prepared = irService_->prepare(block, &error);
            if (!prepared.prepared || !prepared.artifact) {
                ++irStats_.fallbacks;
                return false;
            }
            IrCacheEntry replacement{
                .mappingGeneration = memoryMap_.codeMappingGeneration(),
                .codeBytes = bytes,
                .block = std::move(block),
                .artifact = prepared.artifact,
            };
            found = irCache_.insert_or_assign(cpu_.PC, std::move(replacement)).first;
            ++irStats_.translations;
        }

        if (!guardsMatch(found->second)) {
            ++irStats_.guardFailures;
            irCache_.erase(found);
            return false;
        }

        IR::InterpreterResult result;
        const auto started = std::chrono::steady_clock::now();
        if (!irService_->tryExecute(*found->second.block, *found->second.artifact,
                                    0u, irHost_, &result)) {
            ++irStats_.guardFailures;
            return false;
        }
        irStats_.executionNanos += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        ++irStats_.executions;

        const auto& instruction = found->second.block->instructions.front();
        feedback.pcAfter = cpu_.PC;
        feedback.retiredCycles = result.cycleCondition ? instruction.cyclesTaken
                                                       : instruction.cyclesNotTaken;
        feedback.segmentBoundaryHint = false;
        feedback.isControlFlow = instruction.controlFlow;
        feedback.executionPath = ExecutionPathHint::PortableIr;
        return true;
    }

    uint16_t& registerRef(std::string_view id) {
        if (id == "AF") return cpu_.AF;
        if (id == "BC") return cpu_.BC;
        if (id == "DE") return cpu_.DE;
        if (id == "HL") return cpu_.HL;
        if (id == "IX") return cpu_.IX;
        if (id == "IY") return cpu_.IY;
        if (id == "SP") return cpu_.SP;
        if (id == "PC") return cpu_.PC;
        throw std::invalid_argument("register not found");
    }

    const uint16_t& registerRefConst(std::string_view id) const {
        if (id == "AF") return cpu_.AF;
        if (id == "BC") return cpu_.BC;
        if (id == "DE") return cpu_.DE;
        if (id == "HL") return cpu_.HL;
        if (id == "IX") return cpu_.IX;
        if (id == "IY") return cpu_.IY;
        if (id == "SP") return cpu_.SP;
        if (id == "PC") return cpu_.PC;
        throw std::invalid_argument("register not found");
    }

    Z80Interpreter& cpu_;
    GameGearMemoryMap& memoryMap_;
    bool& romLoaded_;
    Plugin::IExecutorPolicyPlugin*& activePolicy_;
    bool& interruptRequested_;
    GameGearVDP& vdp_;
    CpuFeedback lastFeedback_{};
    GameGearIR::CoreAdapter irAdapter_;
    IR::PortableIrExecutionBackend irBackend_;
    std::unique_ptr<IR::IIrCoreAdapter> dynamicIrAdapter_{};
    std::unique_ptr<IR::IIrExecutionBackend> dynamicIrBackend_{};
    IR::IIrCoreAdapter* activeIrAdapter_;
    IR::IIrExecutionBackend* activeIrBackend_;
    std::unique_ptr<IR::IrExecutionService> irService_;
    GameGearIrHost irHost_;
    std::unordered_map<std::uint16_t, IrCacheEntry> irCache_{};
    GameGearIrStats irStats_{};
};
}

struct GameGearMachine::Impl {
    void flushCartridgeSaveOnSchedule()
    {
        if (!cart) {
            return;
        }

        if (backgroundTaskService == nullptr) {
            (void)saveManager.flush(*cart);
            return;
        }

        if (pendingSaveSnapshot.has_value()) {
            auto shared = std::make_shared<GameGearSaveManager::SaveSnapshot>(std::move(*pendingSaveSnapshot));
            if (backgroundTaskService->submit(BMMQ::BackgroundJobCategory::SaveFlush, [shared]() {
                    GameGearSaveManager::flushSnapshot(*shared);
                })) {
                pendingSaveSnapshot.reset();
            } else {
                pendingSaveSnapshot = std::move(*shared);
                return;
            }
        }

        auto extracted = saveManager.extractDirtySaveSnapshot(*cart);
        if (!extracted.has_value()) {
            return;
        }

        auto shared = std::make_shared<GameGearSaveManager::SaveSnapshot>(std::move(*extracted));
        const bool queued = backgroundTaskService->submit(BMMQ::BackgroundJobCategory::SaveFlush, [shared]() {
            GameGearSaveManager::flushSnapshot(*shared);
        });
        if (!queued) {
            pendingSaveSnapshot = std::move(*shared);
        }
    }

    Z80Interpreter cpu;
    GameGearVDP vdp;
    GameGearPSG psg;
    GameGearInput input;
    std::unique_ptr<GameGearMapper> cart;
    GameGearMemoryMap mem;
    PluginManager pluginManager;
    GameGearSaveManager saveManager;
    BMMQ::BackgroundTaskService* backgroundTaskService = nullptr;
    std::optional<GameGearSaveManager::SaveSnapshot> pendingSaveSnapshot;
    Plugin::DefaultStepPolicy defaultPolicy;
    std::unique_ptr<Plugin::IExecutorPolicyPlugin> ownedPolicy;
    Plugin::IExecutorPolicyPlugin* activePolicy = &defaultPolicy;
    bool romLoaded = false;
    std::optional<std::filesystem::path> pendingRomSourcePath;
    std::optional<uint32_t> lastDigitalInputMask;
    uint64_t inputGeneration = 0u;
    uint64_t stepCounter = 0u;
    uint64_t lastAudioFrameCounter = 0u;
    mutable std::optional<RealtimeAudioPacket> realtimeAudioPacketCache;
    // Interrupt request raised by VDP (VBlank) or other devices. Consumed
    // atomically by the Z80 interrupt provider.
    bool interruptRequested = false;
    GameGearRuntimeContext context{cpu, mem, romLoaded, activePolicy,
                                   interruptRequested, vdp};
};

std::span<const IoRegionDescriptor> GameGearMachine::describeIoRegions() const {
    return kIoRegions;
}

void GameGearMachine::attachExecutorPolicy(const Plugin::IExecutorPolicyPlugin& policy) {
    if (policy.backend() == ExecutionBackend::CachedBlock ||
        policy.backend() == ExecutionBackend::NativeExperimental) {
        throw std::runtime_error("Game Gear does not support the selected execution backend");
    }
    Plugin::validateExecutorPolicyForRuntime(policy, impl->context);
    auto owned = policy.clone();
    if (!owned) {
        throw std::runtime_error("executor policy clone returned null");
    }
    Plugin::validateExecutorPolicyForRuntime(*owned, impl->context);
    impl->ownedPolicy = std::move(owned);
    impl->activePolicy = impl->ownedPolicy.get();
    impl->context.clearIrCache();
}

const Plugin::IExecutorPolicyPlugin& GameGearMachine::attachedExecutorPolicy() const {
    return *impl->activePolicy;
}

uint16_t GameGearMachine::readRegisterPair(std::string_view name) const {
    return impl->context.readRegister16(name);
}

GameGearMachine::GameGearMachine() : impl(std::make_unique<Impl>()) {
    (void)audioService().configureEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 2u,
        .ringBufferCapacitySamples = 4096u,
        .frameChunkSamples = 512u,
    });
    // Wire up CPU memory interface to memory map
    impl->cpu.setMemoryInterface(
        [this](uint16_t addr) { return impl->mem.read(addr); },
        [this](uint16_t addr, uint8_t val) { impl->mem.write(addr, val); }
    );
    impl->cpu.setIoInterface(
        [this](uint8_t port) { return impl->mem.readIoPort(port); },
        [this](uint8_t port, uint8_t value) { impl->mem.writeIoPort(port, value); }
    );
    // Leave memory map using its fallback cartridge until a ROM is loaded.
    impl->mem.setInput(&impl->input);
    impl->mem.setPsg(&impl->psg);
    impl->mem.setVdp(&impl->vdp);
    // Provide a callback for the CPU to atomically consume pending IRQs.
    // Return an optional data byte: present => interrupt pending; empty => none.
    impl->cpu.setInterruptRequestProvider([this]() -> std::optional<uint8_t> {
        if (impl->vdp.isIrqAsserted()) {
            impl->interruptRequested = false;
            // No device-supplied vector byte available; return 0 as a
            // harmless placeholder. IM1 ignores the byte.
            return static_cast<uint8_t>(0u);
        }
        impl->interruptRequested = false;
        return std::nullopt;
    });
    Plugin::validateExecutorPolicyStartup(impl->defaultPolicy);
    impl->vdp.reset();
    impl->input.reset();
    impl->cpu.reset();
    impl->context.clearIrCache();
}
GameGearMachine::~GameGearMachine() {
    (void)flushCartridgeSave();
    if (impl->pluginManager.initialized()) {
        impl->pluginManager.shutdown(view());
    }
}

void GameGearMachine::loadRom(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        throw std::runtime_error("Cannot load empty ROM");
    }
    if (bytes.size() > kMaxRomSize) {
        throw std::runtime_error("ROM too large");
    }
    (void)flushCartridgeSave();
    // Create an appropriate mapper for the ROM and bind it into the
    // memory map. The factory returns a mapper with the ROM already
    // loaded.
    impl->cart = createMapperFromRom(bytes.data(), bytes.size(), impl->pendingRomSourcePath);
    if (!impl->cart) {
        throw std::runtime_error("Failed to create mapper for ROM");
    }
    impl->mem.setCartridge(impl->cart.get());

    if (romPathAllowsSaveBinding(impl->pendingRomSourcePath)) {
        impl->saveManager.bindRomPath(*impl->pendingRomSourcePath);
        impl->saveManager.load(*impl->cart);
    } else {
        impl->saveManager.clearBinding();
    }
    impl->pendingRomSourcePath.reset();
    impl->mem.reset();
    impl->vdp.reset();
    impl->psg.reset();
    impl->input.reset();
    impl->romLoaded = true;
    ++impl->inputGeneration;
    inputService().advanceGeneration(impl->inputGeneration);
    impl->lastDigitalInputMask.reset();
    impl->stepCounter = 0u;
    impl->lastAudioFrameCounter = 0u;
    impl->realtimeAudioPacketCache.reset();
    impl->cpu.reset();
    impl->context.clearIrCache();
    if (impl->pluginManager.size() != 0u) {
        impl->pluginManager.emit(view(), MachineEvent{
            MachineEventType::RomLoaded,
            PluginCategory::System,
            0u,
            0u,
            0u,
            nullptr,
            "ROM loaded"
        });
    }
}

void GameGearMachine::setRomSourcePath(const std::optional<std::filesystem::path>& path) {
    impl->pendingRomSourcePath = path;
}

void GameGearMachine::loadExternalBootRom(const std::vector<uint8_t>& bytes) {
    impl->mem.mapBios(bytes.data(), bytes.size());
}

RuntimeContext& GameGearMachine::runtimeContext() {
    return impl->context;
}

const RuntimeContext& GameGearMachine::runtimeContext() const {
    return impl->context;
}

PluginManager& GameGearMachine::pluginManager() {
    return impl->pluginManager;
}

const PluginManager& GameGearMachine::pluginManager() const {
    return impl->pluginManager;
}

void GameGearMachine::save_state(const std::filesystem::path& path) {
    if (!impl->romLoaded || !impl->cart) {
        throw std::runtime_error("Cannot save Game Gear state before ROM is loaded");
    }

    SaveStateFile state;
    state.header.core_id = BMMQ::kCoreId_GameGear;
    state.header.checksum = BMMQ::SaveStateChecksum::Crc32;
    state.header.rom_hash = BMMQ::crc32(
        impl->cart->romData().data(),
        impl->cart->romData().size());

    state.chunks.push_back(makeChunk("gg.machine", serializeMachineMeta(
        impl->stepCounter,
        impl->lastAudioFrameCounter,
        impl->interruptRequested,
        impl->lastDigitalInputMask,
        impl->inputGeneration)));
    state.chunks.push_back(makeChunk("gg.cpu", impl->cpu.exportState()));
    state.chunks.push_back(makeChunk("gg.memory", impl->mem.exportState()));
    state.chunks.push_back(makeChunk("gg.vdp", impl->vdp.exportState()));
    state.chunks.push_back(makeChunk("gg.psg", impl->psg.exportState()));
    state.chunks.push_back(makeChunk("gg.input", impl->input.exportState()));
    state.chunks.push_back(makeChunk("gg.mapper", impl->cart->exportState()));
    SaveStateReader::write(state, path);
}

void GameGearMachine::load_state(const std::filesystem::path& path) {
    if (!impl->romLoaded || !impl->cart) {
        throw std::runtime_error("Load ROM before loading Game Gear save state");
    }

    const auto state = SaveStateReader::readForCore(
        path, BMMQ::kCoreId_GameGear, impl->cart->romData());
    if (state.header.core_id != BMMQ::kCoreId_GameGear) {
        throw std::invalid_argument("save state is not a Game Gear state");
    }

    const auto& meta = requireChunk(state, "gg.machine").data;
    std::size_t pos = 0;
    const auto nextStepCounter = readU64(meta, pos);
    const auto nextLastAudioFrameCounter = readU64(meta, pos);
    if (pos > meta.size() || meta.size() - pos < 2u) {
        throw std::invalid_argument("Game Gear machine metadata truncated");
    }
    const auto interrupt = meta[pos++];
    const auto hasInput = meta[pos++];
    if (interrupt > 1u || hasInput > 1u) {
        throw std::invalid_argument("Game Gear machine metadata boolean invalid");
    }
    std::optional<uint32_t> nextLastDigitalInputMask;
    if (hasInput != 0u) {
        if (meta.size() - pos < 4u) {
            throw std::invalid_argument("Game Gear machine metadata input truncated");
        }
        nextLastDigitalInputMask =
            static_cast<uint32_t>(meta[pos]) |
            (static_cast<uint32_t>(meta[pos + 1u]) << 8u) |
            (static_cast<uint32_t>(meta[pos + 2u]) << 16u) |
            (static_cast<uint32_t>(meta[pos + 3u]) << 24u);
        pos += 4u;
    }
    const auto nextInputGeneration = readU64(meta, pos);
    if (pos != meta.size()) {
        throw std::invalid_argument("Game Gear machine metadata has trailing data");
    }

    auto nextCart = createMapperFromRom(
        impl->cart->romData().data(),
        impl->cart->romData().size(),
        std::nullopt);
    if (!nextCart) {
        throw std::runtime_error("Failed to recreate mapper for Game Gear save state");
    }
    auto nextMem = impl->mem;
    auto nextVdp = impl->vdp;
    auto nextPsg = impl->psg;
    auto nextInput = impl->input;
    auto nextCpu = impl->cpu;

    nextCart->importState(requireChunk(state, "gg.mapper").data);
    nextMem.importState(requireChunk(state, "gg.memory").data);
    nextVdp.importState(requireChunk(state, "gg.vdp").data);
    nextPsg.importState(requireChunk(state, "gg.psg").data);
    nextInput.importState(requireChunk(state, "gg.input").data);
    nextCpu.importState(requireChunk(state, "gg.cpu").data);

    impl->cart = std::move(nextCart);
    impl->mem = std::move(nextMem);
    impl->vdp = std::move(nextVdp);
    impl->psg = std::move(nextPsg);
    impl->input = std::move(nextInput);
    impl->cpu = std::move(nextCpu);
    impl->context.clearIrCache();
    impl->stepCounter = nextStepCounter;
    impl->lastAudioFrameCounter = nextLastAudioFrameCounter;
    impl->realtimeAudioPacketCache.reset();
    impl->interruptRequested = interrupt != 0u;
    impl->lastDigitalInputMask = nextLastDigitalInputMask;
    impl->inputGeneration = nextInputGeneration;
    impl->mem.setCartridge(impl->cart.get());
    impl->mem.setInput(&impl->input);
    impl->mem.setPsg(&impl->psg);
    impl->mem.setVdp(&impl->vdp);
    inputService().advanceGeneration(impl->inputGeneration);
}

ExecutionSliceResult GameGearMachine::runSlice(
    const ExecutionBudget& budget,
    InstructionRetirementSink* observer) {
    if (!impl->romLoaded) {
        ExecutionSliceResult result;
        result.exitReason = ExecutionSliceExitReason::MachineBoundary;
        return result;
    }
    return Machine::runSlice(budget, observer);
}

void GameGearMachine::step() {
    (void)runSlice(ExecutionBudget{});
}

InstructionRetirementDecision GameGearMachine::onInstructionRetired(
    const CpuFeedback& feedback,
    const ExecutionSliceProgress&)
{
    ++impl->stepCounter;
    impl->vdp.step(feedback.retiredCycles);
    impl->psg.step(feedback.retiredCycles);
    if (impl->vdp.takeScanlineReady() && impl->pluginManager.size() != 0u) {
        const auto vdpScanline = impl->vdp.lastReadyScanline();
        if (vdpScanline >= kGameGearViewportY &&
            vdpScanline < static_cast<uint8_t>(kGameGearViewportY + kGameGearViewportHeight)) {
            impl->pluginManager.emit(view(), MachineEvent{
                MachineEventType::VideoScanlineReady,
                PluginCategory::Video,
                impl->stepCounter,
                0, // No memory-mapped address for scanline
                static_cast<uint8_t>(vdpScanline - kGameGearViewportY),
                &runtimeContext().getLastFeedback(),
                "scanline ready"
            });
        }
    }
    if (impl->vdp.takeVBlankEntered()) {
        if (impl->pluginManager.size() != 0u) {
            impl->pluginManager.emit(view(), MachineEvent{
                MachineEventType::VBlank,
                PluginCategory::Video,
                impl->stepCounter,
                0, // No memory-mapped address for VBlank
                impl->vdp.currentScanline(),
                &runtimeContext().getLastFeedback(),
                "entered VBlank"
            });
        }
        impl->flushCartridgeSaveOnSchedule();
    }
    if (impl->vdp.takeIrqAsserted()) {
        impl->interruptRequested = true;
    }
    const auto audioFrameCounter = impl->psg.frameCounter();
    if (audioFrameCounter != impl->lastAudioFrameCounter) {
        impl->lastAudioFrameCounter = audioFrameCounter;
        impl->realtimeAudioPacketCache.reset();
        if (impl->pluginManager.size() != 0u) {
            impl->pluginManager.emit(view(), MachineEvent{
                MachineEventType::AudioFrameReady,
                PluginCategory::Audio,
                impl->stepCounter,
                0xFF26u,
                runtimeContext().read8(0xFF26u),
                &runtimeContext().getLastFeedback(),
                "psg frame mixed"
            });
        }
    }
    if (impl->interruptRequested || impl->vdp.isIrqAsserted()) {
        return InstructionRetirementDecision::exitSlice(
            ExecutionSliceExitReason::MachineBoundary);
    }
    return InstructionRetirementDecision::continueSlice();
}

void GameGearMachine::serviceInput() {
    if (inputService().state() == InputLifecycleState::Active) {
        (void)inputService().pollActiveAdapter(impl->inputGeneration);
        if (const auto committedInput = inputService().committedDigitalMask(); committedInput.has_value()) {
            const auto pressedMask = *committedInput;
            impl->input.setLogicalButtons(pressedMask);
            impl->lastDigitalInputMask = pressedMask;
            return;
        }
    }

    if (impl->pluginManager.size() == 0u) {
        return;
    }

    if (const auto sampledInput = impl->pluginManager.sampleDigitalInput(view()); sampledInput.has_value()) {
        const auto pressedMask = *sampledInput;
        impl->input.setLogicalButtons(pressedMask);
        impl->lastDigitalInputMask = pressedMask;
    }
}

std::optional<uint32_t> GameGearMachine::currentDigitalInputMask() const {
    return impl->lastDigitalInputMask;
}

std::vector<int16_t> GameGearMachine::recentAudioSamples() const {
    return impl->psg.copyRecentSamples();
}

uint32_t GameGearMachine::audioSampleRate() const {
    return impl->psg.sampleRate();
}

uint8_t GameGearMachine::audioChannelCount() const {
    return impl->psg.outputChannelCount();
}

uint64_t GameGearMachine::audioFrameCounter() const {
    return impl->psg.frameCounter();
}

std::optional<VideoDebugFrameModel> GameGearMachine::videoDebugFrameModel(
    const VideoDebugRenderRequest& request) const
{
    return impl->vdp.buildFrameModel(request);
}

std::optional<RealtimeVideoSubmission> GameGearMachine::realtimeVideoPacket(
    const VideoDebugRenderRequest& request) const
{
    return impl->vdp.buildRealtimeFrame(request);
}

std::optional<RealtimeAudioPacket> GameGearMachine::realtimeAudioPacket() const
{
    const auto frameCounter = impl->psg.frameCounter();
    if (impl->realtimeAudioPacketCache.has_value() &&
        impl->realtimeAudioPacketCache->frameCounter == frameCounter) {
        return impl->realtimeAudioPacketCache;
    }
    RealtimeAudioPacket packet;
    packet.sampleRate = impl->psg.sampleRate();
    packet.channelCount = impl->psg.outputChannelCount();
    packet.frameCounter = frameCounter;
    packet.psgChunksEmitted = impl->psg.chunksEmitted();
    packet.psgSamplesGeneratedTotal = impl->psg.samplesGeneratedTotal();
    packet.psgChunkSamplesLast = static_cast<std::uint32_t>(impl->psg.chunkSamplesLast());
    packet.psgChunkSamplesMin = static_cast<std::uint32_t>(impl->psg.chunkSamplesMin());
    packet.psgChunkSamplesMax = static_cast<std::uint32_t>(impl->psg.chunkSamplesMax());
    packet.psgPendingSamples = static_cast<std::uint32_t>(impl->psg.pendingSamples());
    packet.firstSampleFrame = impl->psg.recentFirstSampleFrame();
    packet.pcmSamples = impl->psg.copyRecentSamples();
    packet.voices = {
        {0u, PsgVoiceKind::Tone}, {1u, PsgVoiceKind::Tone},
        {2u, PsgVoiceKind::Tone}, {3u, PsgVoiceKind::Noise},
    };
    packet.voiceStems = impl->psg.copyRecentVoiceStems();
    packet.events = impl->psg.copyRecentEvents();
    impl->realtimeAudioPacketCache = std::move(packet);
    return impl->realtimeAudioPacketCache;
}

uint32_t GameGearMachine::clockHz() const {
    return impl->context.clockHz();
}

std::string GameGearMachine::stopSummary() const {
    std::ostringstream out;
    out << "PC=0x" << std::hex << std::uppercase << impl->cpu.PC
        << " SP=0x" << impl->cpu.SP
        << " AF=0x" << impl->cpu.AF
        << " LY=0x" << static_cast<int>(impl->vdp.currentScanline())
        << std::dec << '\n'
        << "Input port=0x" << std::hex << std::uppercase << static_cast<int>(impl->input.readInputs())
        << std::dec << '\n'
        << "MEM=0x" << std::hex << std::uppercase << static_cast<int>(impl->mem.memoryControlValue())
        << " IO=0x" << static_cast<int>(impl->mem.ioControlValue())
        << " BIOS=" << (impl->mem.hasBios() ? "Yes" : "No") << std::dec << '\n';

    if (impl->cart) {
        if (auto* concrete = dynamic_cast<GameGearCartridge*>(impl->cart.get())) {
            const auto banks = concrete->bankRegisters();
            out << "Cart CTRL=0x" << std::hex << std::uppercase << static_cast<int>(concrete->controlRegister())
                << " banks=[" << std::dec << static_cast<int>(banks[0]) << "," << static_cast<int>(banks[1]) << "," << static_cast<int>(banks[2]) << "]\n";
        }
    }
    return out.str();
}

bool GameGearMachine::flushCartridgeSave() {
    if (!impl->cart) return false;
    return impl->saveManager.flush(*impl->cart);
}

void GameGearMachine::flushPendingBackgroundWork()
{
    if (impl->pendingSaveSnapshot.has_value()) {
        GameGearSaveManager::flushSnapshot(*impl->pendingSaveSnapshot);
        impl->pendingSaveSnapshot.reset();
    }
    (void)flushCartridgeSave();
}

void GameGearMachine::setBackgroundTaskService(BMMQ::BackgroundTaskService* service) noexcept {
    impl->backgroundTaskService = service;
}

bool GameGearMachine::cpuInterruptsEnabled() const {
    return impl->cpu.IME;
}

GameGearIrStats GameGearMachine::irStats() const noexcept {
    return impl->context.irStats();
}

std::uint32_t GameGearMachine::irArchitectureId() const noexcept {
    return GameGearIR::kArchitectureId;
}

void GameGearMachine::setIrComponents(
    std::unique_ptr<IR::IIrCoreAdapter> adapter,
    std::unique_ptr<IR::IIrExecutionBackend> backend) {
    impl->context.setIrComponents(std::move(adapter), std::move(backend));
}

} // namespace BMMQ
