#pragma once
// Nintendo Game Boy machine abstraction.
// Matches the GameGearMachine architecture: thin public interface,
// Impl pimpl struct, separate subsystem classes.
//
// Architecture:
//   GameBoyMachine (public API)
//     -> Impl (pimpl)
//         -> cpu_ (LR3592_DMG via LR3592_PluginRuntime wrapper)
//         -> memoryMap_ (GameBoyMemoryMap)
//         -> ppu_ (GameBoyPPU)
//         -> apu_ (GameBoyAPU)
//         -> input_ (GameBoyInput)
//         -> mapper_ (GameBoyMapper)
//         -> pluginManager_ (PluginManager)
//         -> context_ (GameBoyRuntimeContext)

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <atomic>

#include "../../machine/Machine.hpp"
#include "../../machine/RuntimeContext.hpp"
#include "../../machine/plugins/IoPlugin.hpp"
#include "../../machine/plugins/PluginManager.hpp"
#include "../../machine/BackgroundTaskService.hpp"
#include "gameboy_plugin_runtime.hpp"
#include "GameBoyMemoryMap.hpp"
#include "GameBoyPPU.hpp"
#include "GameBoyAPU.hpp"
#include "GameBoyInput.hpp"
#include "GameBoyMapper.hpp"
#include "cartridge/GameBoyCartridge.hpp"
#include "cartridge/CartridgeSaveManager.hpp"

namespace GB {

// Forward declaration — defined in GameBoyMachine.cpp
class GameBoyRuntimeContext;

class GameBoyMachine final : public BMMQ::Machine,
                             public BMMQ::IExternalBootRomMachine,
                             public BMMQ::IRomPathAwareMachine {
public:
    GameBoyMachine();
    ~GameBoyMachine() override;

    // Machine interface
    void loadRom(const std::vector<uint8_t>& bytes) override;
    void loadRomFromPath(const std::filesystem::path& path);
    BMMQ::RuntimeContext& runtimeContext() override;
    const BMMQ::RuntimeContext& runtimeContext() const override;
    BMMQ::PluginManager& pluginManager() override;
    const BMMQ::PluginManager& pluginManager() const override;
    void save_state(const std::filesystem::path& path) override;
    void load_state(const std::filesystem::path& path) override;
    // Stable guest-visible state fingerprint for cross-backend corpus checks.
    // Executor-path metadata and host-side cache/profiling state are excluded.
    [[nodiscard]] std::string deterministicStateFingerprint() const;

    // IExternalBootRomMachine
    void loadExternalBootRom(const std::vector<uint8_t>& bytes) override;

    // IRomPathAwareMachine
    void setRomSourcePath(const std::optional<std::filesystem::path>& path) override;

    // IoPlugin interface
    std::span<const BMMQ::IoRegionDescriptor> describeIoRegions() const override;
    void attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin& policy) override;
    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override;

    // Audio queries
    std::vector<int16_t> recentAudioSamples() const override;
    uint32_t audioSampleRate() const override;
    uint8_t audioChannelCount() const override;
    uint64_t audioFrameCounter() const override;
    std::string_view visualTargetId() const noexcept override;
    const BMMQ::IVisualDebugAdapter* visualDebugAdapter() const noexcept override;

    // Input queries
    std::optional<uint32_t> currentDigitalInputMask() const override;

    // Video debug
    std::optional<BMMQ::VideoDebugFrameModel> videoDebugFrameModel(
        const BMMQ::VideoDebugRenderRequest& request) const override;
    std::optional<BMMQ::RealtimeVideoSubmission> realtimeVideoPacket(
        const BMMQ::VideoDebugRenderRequest& request) const override;
    std::optional<BMMQ::VideoStateView> videoStateSnapshot() const override;
    std::optional<BMMQ::RealtimeAudioPacket> realtimeAudioPacket() const override;

    // Cartridge access (for tests)
    [[nodiscard]] GameBoyCartridge& cartridge() { return impl_->cartridge_; }
    [[nodiscard]] const GameBoyCartridge& cartridge() const { return impl_->cartridge_; }

    // Save management
    bool flushCartridgeSave();
    void flushPendingBackgroundWork() override;
    void setBackgroundTaskService(BMMQ::BackgroundTaskService* service) noexcept {
        impl_->backgroundTaskService = service;
    }

    // Step
    BMMQ::ExecutionSliceResult runSlice(
        const BMMQ::ExecutionBudget& budget,
        BMMQ::InstructionRetirementSink* observer = nullptr) override;
    void step() override;
    void serviceInput() override;

    // Joypad control
    void setJoypadState(uint8_t value);

    // Boot ROM (legacy alias)
    void loadBootRom(const std::vector<uint8_t>& bytes) { loadExternalBootRom(bytes); }

    // Emulation-lane translated block-cache diagnostics.
    struct BlockCacheStats {
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> misses{0};
        std::atomic<uint64_t> invalidations{0};
        std::atomic<uint64_t> translations{0};
        std::atomic<uint64_t> translatedInstructions{0};
        std::atomic<uint64_t> guardFailures{0};
        std::atomic<uint64_t> chainContinuations{0};
        std::atomic<uint64_t> unsupportedFallbacks{0};
        std::atomic<uint64_t> irTranslations{0};
        std::atomic<uint64_t> irExecutions{0};
        std::atomic<uint64_t> irGuardFailures{0};
        std::atomic<uint64_t> irFallbacks{0};
        std::atomic<uint64_t> irLoweredInstructions{0};
        std::atomic<uint64_t> irIneligibleTranslations{0};
        std::atomic<uint64_t> irLoweringNanos{0};
        std::atomic<uint64_t> irGuardChecks{0};
        std::atomic<uint64_t> irGuardCheckNanos{0};
        std::atomic<uint64_t> irExecutionNanos{0};
        std::atomic<uint64_t> irBlockEntries{0};
        std::atomic<uint64_t> irBlockContinuations{0};
        std::atomic<uint64_t> irBlockContinuationRejects{0};
        BlockCacheStats() = default;
        BlockCacheStats(uint64_t h, uint64_t m, uint64_t i,
                        uint64_t t = 0, uint64_t ti = 0, uint64_t g = 0,
                        uint64_t c = 0, uint64_t u = 0,
                        uint64_t irt = 0, uint64_t ire = 0,
                        uint64_t irg = 0, uint64_t irf = 0,
                        uint64_t irli = 0, uint64_t irit = 0,
                        uint64_t irln = 0, uint64_t irgc = 0,
                        uint64_t irgn = 0, uint64_t iren = 0,
                        uint64_t irbe = 0, uint64_t irbc = 0,
                        uint64_t irbr = 0)
            : hits(h), misses(m), invalidations(i), translations(t),
              translatedInstructions(ti), guardFailures(g),
              chainContinuations(c), unsupportedFallbacks(u),
              irTranslations(irt), irExecutions(ire),
              irGuardFailures(irg), irFallbacks(irf),
              irLoweredInstructions(irli), irIneligibleTranslations(irit),
              irLoweringNanos(irln), irGuardChecks(irgc), irGuardCheckNanos(irgn),
              irExecutionNanos(iren), irBlockEntries(irbe),
              irBlockContinuations(irbc), irBlockContinuationRejects(irbr) {}
        BlockCacheStats(const BlockCacheStats& other)
            : hits(other.hits.load()), misses(other.misses.load()),
              invalidations(other.invalidations.load()),
              translations(other.translations.load()),
              translatedInstructions(other.translatedInstructions.load()),
              guardFailures(other.guardFailures.load()),
              chainContinuations(other.chainContinuations.load()),
              unsupportedFallbacks(other.unsupportedFallbacks.load()),
              irTranslations(other.irTranslations.load()),
              irExecutions(other.irExecutions.load()),
              irGuardFailures(other.irGuardFailures.load()),
              irFallbacks(other.irFallbacks.load()),
              irLoweredInstructions(other.irLoweredInstructions.load()),
              irIneligibleTranslations(other.irIneligibleTranslations.load()),
              irLoweringNanos(other.irLoweringNanos.load()),
              irGuardChecks(other.irGuardChecks.load()),
              irGuardCheckNanos(other.irGuardCheckNanos.load()),
              irExecutionNanos(other.irExecutionNanos.load()),
              irBlockEntries(other.irBlockEntries.load()),
              irBlockContinuations(other.irBlockContinuations.load()),
              irBlockContinuationRejects(other.irBlockContinuationRejects.load()) {}
        BlockCacheStats& operator=(const BlockCacheStats& other) {
            hits.store(other.hits.load());
            misses.store(other.misses.load());
            invalidations.store(other.invalidations.load());
            translations.store(other.translations.load());
            translatedInstructions.store(other.translatedInstructions.load());
            guardFailures.store(other.guardFailures.load());
            chainContinuations.store(other.chainContinuations.load());
            unsupportedFallbacks.store(other.unsupportedFallbacks.load());
            irTranslations.store(other.irTranslations.load());
            irExecutions.store(other.irExecutions.load());
            irGuardFailures.store(other.irGuardFailures.load());
            irFallbacks.store(other.irFallbacks.load());
            irLoweredInstructions.store(other.irLoweredInstructions.load());
            irIneligibleTranslations.store(other.irIneligibleTranslations.load());
            irLoweringNanos.store(other.irLoweringNanos.load());
            irGuardChecks.store(other.irGuardChecks.load());
            irGuardCheckNanos.store(other.irGuardCheckNanos.load());
            irExecutionNanos.store(other.irExecutionNanos.load());
            irBlockEntries.store(other.irBlockEntries.load());
            irBlockContinuations.store(other.irBlockContinuations.load());
            irBlockContinuationRejects.store(other.irBlockContinuationRejects.load());
            return *this;
        }
    };
    [[nodiscard]] BlockCacheStats blockCacheStats() const;
    [[nodiscard]] bool blockCacheEnabled() const;
    void setBlockCacheEnabled(bool enabled);
    [[nodiscard]] bool portableIrEnabled() const;
    void setPortableIrEnabled(bool enabled);
    [[nodiscard]] bool nativeIrEnabled() const;
    [[nodiscard]] bool nativeIrSupported() const;
    void setNativeIrEnabled(bool enabled);
    [[nodiscard]] bool detailedIrTimingEnabled() const;
    void setDetailedIrTimingEnabled(bool enabled);

    // Register access
    uint16_t readRegisterPair(std::string_view id) const override;
    std::string stopSummary() const override;

protected:
    BMMQ::InstructionRetirementDecision onInstructionRetired(
        const BMMQ::CpuFeedback& feedback,
        const BMMQ::ExecutionSliceProgress& progress) override;

private:
    struct Impl {
        LR3592_PluginRuntime cpu;
        GameBoyMemoryMap memoryMap;
        GameBoyPPU ppu;
        GameBoyAPU apu;
        GameBoyInput input;
        GameBoyMapper mapper;
        GB::GameBoyCartridge cartridge_;
        GB::CartridgeSaveManager saveManager;
        BMMQ::PluginManager pluginManager;
        BMMQ::BackgroundTaskService* backgroundTaskService = nullptr;
        std::optional<GB::CartridgeSaveManager::SaveSnapshot> pendingSaveSnapshot;
        bool romLoaded = false;
        uint32_t romHash = 0u;
        std::optional<std::filesystem::path> pendingRomSourcePath;
        std::optional<uint32_t> lastDigitalInputMask;
        uint64_t inputGeneration = 0u;
        uint64_t stepCounter = 0u;
        uint64_t lastAudioFrameCounter = 0u;
        bool bootEntryPending = false;
        // Interrupt request raised by PPU (VBlank). Consumed atomically by CPU.
        bool interruptRequested = false;
        // Runtime context wrapping CPU + memory map
        std::unique_ptr<GameBoyRuntimeContext> context;
        BMMQ::Plugin::DefaultStepPolicy defaultPolicy;
        BMMQ::Plugin::IExecutorPolicyPlugin* activePolicy = &defaultPolicy;
    };

    std::unique_ptr<Impl> impl_;
};

} // namespace GB
