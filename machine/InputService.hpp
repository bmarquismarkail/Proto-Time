#ifndef BMMQ_INPUT_SERVICE_HPP
#define BMMQ_INPUT_SERVICE_HPP

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "plugins/input/InputEngine.hpp"
#include "plugins/input/InputPlugin.hpp"

namespace BMMQ {

enum class InputLifecycleState {
    Detached = 0,
    Paused,
    Active,
    Faulted,
};

struct InputServiceDiagnostics {
    std::size_t pollFailureCount = 0;
    std::size_t eventOverflowCount = 0;
    std::size_t neutralFallbackCount = 0;
    std::size_t staleGenerationDropCount = 0;
    uint64_t lastCommittedGeneration = 0;
    std::string activeAdapterSummary;
    std::string activeMappingProfile;
    std::string lastBackendError;
    InputLifecycleState state = InputLifecycleState::Detached;
};

class InputService {
public:
    InputService() = default;

    explicit InputService(InputEngineConfig config)
        : engine_(std::move(config)) {}

    [[nodiscard]] const InputEngine& engine() const noexcept
    {
        return engine_;
    }

    [[nodiscard]] std::optional<InputButtonMask> committedDigitalMask() const noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return engine_.committedDigitalMask();
    }

    [[nodiscard]] std::optional<InputAnalogState> committedAnalogState() const noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return engine_.committedAnalogState();
    }

    [[nodiscard]] uint64_t currentGeneration() const noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return engine_.currentGeneration();
    }

    void advanceGeneration(uint64_t generation) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        engine_.advanceGeneration(generation);
        syncDiagnostics();
    }

    void publishDigitalSnapshot(InputButtonMask mask, uint64_t generation) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (engine_.stageDigitalSnapshot(mask, generation)) {
            (void)engine_.commitDigitalSnapshot();
        }
        syncDiagnostics();
    }

    void applyNeutralFallback(uint64_t generation) noexcept
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        engine_.applyNeutralFallback(generation);
        syncDiagnostics();
    }

    [[nodiscard]] InputLifecycleState state() const
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return state_;
    }

    [[nodiscard]] InputServiceDiagnostics diagnostics() const
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        syncDiagnostics();
        return diagnostics_;
    }

    [[nodiscard]] bool configureMappingProfile(std::string profileName)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (!configurationAllowed()) {
            return false;
        }
        mappingProfileName_ = std::move(profileName);
        diagnostics_.activeMappingProfile = mappingProfileName_;
        return true;
    }

    [[nodiscard]] bool attachAdapter(std::unique_ptr<IInputPlugin> adapter)
    {
        if (adapter == nullptr) {
            return false;
        }

        const auto caps = adapter->capabilities();
        if (!validateCapabilities(caps)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return performAttachForStateLocked(*adapter, caps, [&adapter, this]() {
            ownedAdapter_ = std::move(adapter);
            adapter_ = ownedAdapter_.get();
        });
    }

    [[nodiscard]] bool attachExternalAdapter(IInputPlugin& adapter)
    {
        const auto caps = adapter.capabilities();
        if (!validateCapabilities(caps)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        return performAttachForStateLocked(adapter, caps, [&adapter, this]() {
            ownedAdapter_.reset();
            adapter_ = &adapter;
        });
    }

    [[nodiscard]] bool detachAdapter(uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (state_ == InputLifecycleState::Detached) {
            return false;
        }
        detachCurrentAdapterLocked();
        engine_.applyNeutralFallback(generation);
        diagnostics_.activeAdapterSummary.clear();
        diagnostics_.lastBackendError.clear();
        setState(InputLifecycleState::Detached);
        syncDiagnostics();
        return true;
    }

    [[nodiscard]] bool resume()
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (adapter_ == nullptr) {
            setState(InputLifecycleState::Detached);
            return false;
        }

        const auto caps = adapter_->capabilities();
        if (!isLiveCompatible(caps)) {
            diagnostics_.lastBackendError = "input adapter is not live-safe";
            setState(InputLifecycleState::Faulted);
            return false;
        }
        if (!adapter_->open()) {
            diagnostics_.lastBackendError = std::string(adapter_->lastError());
            setState(InputLifecycleState::Faulted);
            return false;
        }
        diagnostics_.activeAdapterSummary = std::string(adapter_->name());
        diagnostics_.lastBackendError.clear();
        setState(InputLifecycleState::Active);
        return true;
    }

    [[nodiscard]] bool pause()
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (adapter_ != nullptr) {
            adapter_->close();
            setState(InputLifecycleState::Paused);
        } else {
            setState(InputLifecycleState::Detached);
        }
        return true;
    }

    [[nodiscard]] bool pollActiveAdapter(uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        if (state_ != InputLifecycleState::Active || adapter_ == nullptr) {
            return false;
        }

        bool stagedAny = false;
        if (auto* digital = dynamic_cast<IDigitalInputSourcePlugin*>(adapter_); digital != nullptr) {
            if (const auto sample = digital->sampleDigitalInput(); sample.has_value()) {
                stagedAny = engine_.stageDigitalSnapshot(*sample, generation) || stagedAny;
            }
        }
        if (auto* analog = dynamic_cast<IAnalogInputSourcePlugin*>(adapter_); analog != nullptr) {
            if (const auto sample = analog->sampleAnalogInput(); sample.has_value()) {
                stagedAny = engine_.stageAnalogSnapshot(*sample, generation) || stagedAny;
            }
        }
        if (stagedAny) {
            stagedAny = engine_.commitSnapshots();
        }
        syncDiagnostics();
        return stagedAny;
    }

    void recordPollFailure(std::string_view error = {})
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        engine_.notePollFailure();
        if (!error.empty()) {
            diagnostics_.lastBackendError = std::string(error);
        }
        syncDiagnostics();
    }

    void recordBackendLoss(std::string_view error, uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(nonRealTimeMutex_);
        engine_.applyNeutralFallback(generation);
        diagnostics_.lastBackendError = std::string(error);
        setState(InputLifecycleState::Faulted);
        syncDiagnostics();
    }

private:
    [[nodiscard]] static bool isLiveCompatible(const InputPluginCapabilities& caps) noexcept
    {
        return caps.pollingSafe && caps.deterministic && !caps.nonRealtimeOnly;
    }

    [[nodiscard]] static bool validateCapabilities(const InputPluginCapabilities& caps) noexcept
    {
        return caps.fixedLogicalLayout && (caps.supportsDigital || caps.supportsAnalog);
    }

    template<typename AttachFn>
    [[nodiscard]] bool performAttachForStateLocked(IInputPlugin& adapter,
                                                   const InputPluginCapabilities& caps,
                                                   AttachFn&& attachFn)
    {
        if (state_ == InputLifecycleState::Active) {
            if (adapter_ == nullptr) {
                return false;
            }
            const auto currentCaps = adapter_->capabilities();
            if (!currentCaps.hotSwapSafe || !caps.hotSwapSafe || !isLiveCompatible(caps)) {
                return false;
            }
            if (!adapter.open()) {
                diagnostics_.lastBackendError = std::string(adapter.lastError());
                return false;
            }
            adapter.close();
            std::forward<AttachFn>(attachFn)();
            diagnostics_.activeAdapterSummary = std::string(adapter.name());
            diagnostics_.lastBackendError.clear();
            setState(InputLifecycleState::Active);
            return true;
        }

        detachCurrentAdapterLocked();
        std::forward<AttachFn>(attachFn)();
        diagnostics_.activeAdapterSummary = std::string(adapter.name());
        diagnostics_.lastBackendError.clear();
        setState(InputLifecycleState::Paused);
        return true;
    }

    [[nodiscard]] bool configurationAllowed() const noexcept
    {
        return state_ != InputLifecycleState::Active;
    }

    void setState(InputLifecycleState state) noexcept
    {
        state_ = state;
        diagnostics_.state = state_;
    }

    void detachCurrentAdapterLocked() noexcept
    {
        if (adapter_ != nullptr) {
            adapter_->close();
        }
        adapter_ = nullptr;
        ownedAdapter_.reset();
    }

    void syncDiagnostics() const noexcept
    {
        const auto& stats = engine_.stats();
        diagnostics_.pollFailureCount = stats.pollFailureCount;
        diagnostics_.eventOverflowCount = stats.eventOverflowCount;
        diagnostics_.neutralFallbackCount = stats.neutralFallbackCount;
        diagnostics_.staleGenerationDropCount = stats.staleGenerationDropCount;
        diagnostics_.lastCommittedGeneration = stats.lastCommittedGeneration;
        diagnostics_.state = state_;
        if (adapter_ == nullptr && state_ == InputLifecycleState::Detached) {
            diagnostics_.activeAdapterSummary.clear();
        }
    }

    InputEngine engine_{};
    IInputPlugin* adapter_ = nullptr;
    std::unique_ptr<IInputPlugin> ownedAdapter_{};
    std::string mappingProfileName_{};
    InputLifecycleState state_ = InputLifecycleState::Detached;
    mutable InputServiceDiagnostics diagnostics_{};
    mutable std::mutex nonRealTimeMutex_;
};

} // namespace BMMQ

#endif // BMMQ_INPUT_SERVICE_HPP