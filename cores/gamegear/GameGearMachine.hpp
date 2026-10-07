
#ifndef GAMEGEAR_MACHINE_HPP
#define GAMEGEAR_MACHINE_HPP
#include "machine/plugins/debug/DebugMachine.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "machine/Machine.hpp"
#include "machine/modding/NativeMod.hpp"
#include "space/ExecutionContract.hpp"

namespace BMMQ {

class BackgroundTaskService;

struct GameGearIrStats {
    std::uint64_t dispatchAttempts = 0u;
    std::uint64_t translations = 0u;
    std::uint64_t executions = 0u;
    std::uint64_t guardChecks = 0u;
    std::uint64_t guardFailures = 0u;
    std::uint64_t unsupportedFallbacks = 0u;
    std::uint64_t fallbacks = 0u;
    std::uint64_t cacheReuses = 0u;
    std::uint64_t loweringNanos = 0u;
    std::uint64_t guardCheckNanos = 0u;
    std::uint64_t executionNanos = 0u;
};

class GameGearMachine final : public Machine,
                              public IRomPathAwareMachine,
                              public IExternalBootRomMachine,
                              public IIrComponentAwareMachine,
                              public Debug::IDebugMachineV1 {
public:
    GameGearMachine();
    void connectDebugEngine(BMMQ::Debug::DebugEngine*) override;
    bool debugPortBus() const noexcept override { return true; }
    std::span<const char* const> debugRegisterNames() const noexcept override;
    std::array<uint16_t,20> debugRegisters() const override;
    bool debugValidateRegisters(const std::array<uint16_t,20>&) const noexcept override;
    void debugCommitRegisters(const std::array<uint16_t,20>&) noexcept override;
    bool debugPeek(uint16_t, uint8_t&) const noexcept override;
    bool debugWritable(uint16_t) const noexcept override;
    void debugCommitByte(uint16_t, uint8_t) noexcept override;
    uint64_t debugBacking(uint16_t) const noexcept override;
    void debugEdited() noexcept override;

    MemoryPool<uint16_t,uint8_t,uint16_t>& executionMemory();
    uint64_t analysisLocation(uint16_t,bool write=false)const;
    uint8_t analysisRead(uint16_t)const;
    bool analysisRam(uint16_t)const;
    size_t analysisRamCapacity()const noexcept;
    bool analysisPhysicalRamByte(size_t,uint8_t&)const noexcept;
    bool snapshotBoundaryOnly()const;
    std::span<const uint8_t> snapshotInstruction();
    std::string deterministicStateFingerprint()const;
    void setAnalysisCapture(Space::Capture*);
    void setSnapshotExecution(Space::ExecutionController*);
    void setAnalysisInput(uint8_t);
    ~GameGearMachine() override;

    void loadRom(const std::vector<uint8_t>& bytes) override;
    [[nodiscard]] const std::vector<uint8_t>& romData() const noexcept;
    void setRomSourcePath(const std::optional<std::filesystem::path>& path) override;
    RuntimeContext& runtimeContext() override;
    const RuntimeContext& runtimeContext() const override;
    PluginManager& pluginManager() override;
    const PluginManager& pluginManager() const override;
    void save_state(const std::filesystem::path& path) override;
    void load_state(const std::filesystem::path& path) override;
    ExecutionSliceResult runSlice(
        const ExecutionBudget& budget,
        InstructionRetirementSink* observer = nullptr) override;
    void step() override;
    void serviceInput() override;

    std::span<const IoRegionDescriptor> describeIoRegions() const override;
    void attachExecutorPolicy(const Plugin::IExecutorPolicyPlugin& policy) override;
    const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override;
    uint16_t readRegisterPair(std::string_view id) const override;
    std::optional<uint32_t> currentDigitalInputMask() const override;
    std::vector<int16_t> recentAudioSamples() const override;
    uint32_t audioSampleRate() const override;
    uint8_t audioChannelCount() const override;
    uint64_t audioFrameCounter() const override;
    std::string_view visualTargetId() const noexcept override;
    const IVisualDebugAdapter* visualDebugAdapter() const noexcept override;
    std::optional<VideoDebugFrameModel> videoDebugFrameModel(
        const VideoDebugRenderRequest& request) const override;
    std::optional<RealtimeVideoSubmission> realtimeVideoPacket(
        const VideoDebugRenderRequest& request) const override;
    std::optional<VideoStateView> videoStateSnapshot() const override;
    std::optional<RealtimeAudioPacket> realtimeAudioPacket() const override;
    uint32_t clockHz() const override;
    std::string stopSummary() const override;
    [[nodiscard]] bool flushCartridgeSave();
    void flushPendingBackgroundWork() override;
    void setBackgroundTaskService(BMMQ::BackgroundTaskService* service) noexcept;
    bool installNativeTrampoline(std::uint16_t address, std::uint8_t romBank,
                                 std::unique_ptr<Modding::NativeMod> module,
                                 std::uint32_t hookId);
    void clearNativeTrampolines() noexcept;
    void loadExternalBootRom(const std::vector<uint8_t>& bytes) override;
    // Test helper: inspect whether CPU IME is currently set.
    bool cpuInterruptsEnabled() const;
    [[nodiscard]] GameGearIrStats irStats() const noexcept;
    [[nodiscard]] bool detailedIrTimingEnabled() const noexcept;
    void setDetailedIrTimingEnabled(bool enabled) noexcept;
    [[nodiscard]] std::uint32_t irArchitectureId() const noexcept override;
    void setIrComponents(std::unique_ptr<IR::IIrCoreAdapter> adapter,
                         std::unique_ptr<IR::IIrExecutionBackend> backend) override;

protected:
    InstructionRetirementDecision onInstructionRetired(
        const CpuFeedback& feedback,
        const ExecutionSliceProgress& progress) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace BMMQ

#endif // GAMEGEAR_MACHINE_HPP
