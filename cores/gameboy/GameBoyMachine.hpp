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
    std::optional<BMMQ::RealtimeVideoPacket> realtimeVideoPacket(
        const BMMQ::VideoDebugRenderRequest& request) const override;
    std::optional<BMMQ::RealtimeAudioPacket> realtimeAudioPacket() const override;

    // Cartridge access (for tests)
    [[nodiscard]] GameBoyCartridge& cartridge() { return impl_->cartridge_; }
    [[nodiscard]] const GameBoyCartridge& cartridge() const { return impl_->cartridge_; }

    // Save management
    bool flushCartridgeSave();
    void setBackgroundTaskService(BMMQ::BackgroundTaskService* service) noexcept {
        impl_->backgroundTaskService = service;
    }

    // Step
    void step() override;
    void serviceInput() override;

    // Joypad control
    void setJoypadState(uint8_t value);

    // Boot ROM (legacy alias)
    void loadBootRom(const std::vector<uint8_t>& bytes) { loadExternalBootRom(bytes); }

    // Block cache stats (stub for compatibility)
    struct BlockCacheStats {
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> misses{0};
        std::atomic<uint64_t> invalidations{0};
        BlockCacheStats() = default;
        BlockCacheStats(uint64_t h, uint64_t m, uint64_t i)
            : hits(h), misses(m), invalidations(i) {}
        BlockCacheStats(const BlockCacheStats& other)
            : hits(other.hits.load()), misses(other.misses.load()),
              invalidations(other.invalidations.load()) {}
        BlockCacheStats& operator=(const BlockCacheStats& other) {
            hits.store(other.hits.load());
            misses.store(other.misses.load());
            invalidations.store(other.invalidations.load());
            return *this;
        }
    };
    [[nodiscard]] BlockCacheStats blockCacheStats() const;
    [[nodiscard]] bool blockCacheEnabled() const;
    void setBlockCacheEnabled(bool enabled);

    // Register access
    uint16_t readRegisterPair(std::string_view id) const override;
    std::string stopSummary() const override;

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
