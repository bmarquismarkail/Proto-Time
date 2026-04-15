#include <cassert>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "machine/InputService.hpp"

#define ASSERT_DIAG_EQ(expected, actualExpr) \
    do { \
        const std::string expectedValue = (expected); \
        const auto actualValue = (actualExpr); \
        if (actualValue != expectedValue) { \
            std::cerr << "expected diagnostic '" << expectedValue \
                      << "' but got '" << actualValue << "'" << '\n'; \
            assert(actualValue == expectedValue); \
            return 1; \
        } \
    } while (false)

namespace {

class TestInputAdapter final : public BMMQ::IDigitalInputSourcePlugin,
                               public BMMQ::IAnalogInputSourcePlugin {
public:
    explicit TestInputAdapter(BMMQ::InputPluginCapabilities capabilities,
                              BMMQ::InputButtonMask digitalMask = 0u,
                              BMMQ::InputAnalogState analogState = {})
        : capabilities_(capabilities),
          digitalMask_(digitalMask),
          analogState_(analogState) {}

    [[nodiscard]] BMMQ::InputPluginCapabilities capabilities() const noexcept override
    {
        return capabilities_;
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return name_;
    }

    [[nodiscard]] bool open() override
    {
        ++openCount_;
        return openSucceeds_;
    }

    void close() noexcept override
    {
        ++closeCount_;
    }

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return lastError_;
    }

    [[nodiscard]] std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override
    {
        if (!capabilities_.supportsDigital) {
            return std::nullopt;
        }
        return digitalMask_;
    }

    [[nodiscard]] std::optional<BMMQ::InputAnalogState> sampleAnalogInput() override
    {
        if (!capabilities_.supportsAnalog) {
            return std::nullopt;
        }
        return analogState_;
    }

    void setOpenSucceeds(bool openSucceeds, std::string error = {})
    {
        openSucceeds_ = openSucceeds;
        lastError_ = std::move(error);
    }

    [[nodiscard]] int openCount() const noexcept
    {
        return openCount_;
    }

    [[nodiscard]] int closeCount() const noexcept
    {
        return closeCount_;
    }

private:
    BMMQ::InputPluginCapabilities capabilities_{};
    BMMQ::InputButtonMask digitalMask_ = 0u;
    BMMQ::InputAnalogState analogState_{};
    bool openSucceeds_ = true;
    int openCount_ = 0;
    int closeCount_ = 0;
    std::string lastError_{};
    std::string name_ = "test-input-adapter";
};

} // namespace

int main()
{
    #ifdef NDEBUG
    std::cerr << "DBG: NDEBUG defined" << '\n';
    #else
    std::cerr << "DBG: NDEBUG not defined" << '\n';
    #endif
    BMMQ::InputService service({
        .stagingCapacity = 2,
    });

    assert(service.state() == BMMQ::InputLifecycleState::Detached);
    assert(service.configureMappingProfile("default-gameboy"));
    assert(service.diagnostics().activeMappingProfile == "default-gameboy");

    auto safeCaps = BMMQ::InputPluginCapabilities{
        .pollingSafe = true,
        .eventPumpSafe = true,
        .deterministic = true,
        .supportsDigital = true,
        .supportsAnalog = true,
        .fixedLogicalLayout = true,
        .hotSwapSafe = false,
        .liveSeek = false,
        .nonRealtimeOnly = false,
        .headlessSafe = true,
    };
    BMMQ::InputAnalogState analog{};
    analog.channels[1] = 77;
    auto safeAdapter = std::make_unique<TestInputAdapter>(safeCaps, 0x10u, analog);
    assert(service.attachAdapter(std::move(safeAdapter)));
    assert(service.state() == BMMQ::InputLifecycleState::Paused);
    assert(service.resume());
    assert(service.state() == BMMQ::InputLifecycleState::Active);
    assert(service.pollActiveAdapter(1u));
    assert(service.committedDigitalMask().has_value());
    assert(*service.committedDigitalMask() == 0x10u);
    assert(service.committedAnalogState().has_value());
    assert(service.committedAnalogState()->channels[1] == 77);
    assert(service.diagnostics().lastCommittedGeneration == 1u);
    assert(!service.configureMappingProfile("active-remap"));

    auto unsafeCaps = safeCaps;
    unsafeCaps.pollingSafe = false;
    auto unsafeAdapter = std::make_unique<TestInputAdapter>(unsafeCaps, 0x20u);
    assert(!service.attachAdapter(std::move(unsafeAdapter)));
    assert(service.state() == BMMQ::InputLifecycleState::Active);

    service.recordPollFailure("temporary poll failure");
    assert(service.diagnostics().pollFailureCount == 1u);
    assert(service.committedDigitalMask().has_value());
    assert(*service.committedDigitalMask() == 0x10u);

    service.recordBackendLoss("input backend detached", 2u);
    assert(service.state() == BMMQ::InputLifecycleState::Faulted);
    std::cerr << "DBG: after recordBackendLoss diagnostics.lastBackendError='" << service.diagnostics().lastBackendError << "' state=" << static_cast<int>(service.state()) << '\n';
    assert(service.diagnostics().neutralFallbackCount == 1u);
    assert(service.committedDigitalMask().has_value());
    assert(*service.committedDigitalMask() == 0u);

    assert(service.pause());
    std::cerr << "DBG: after pause diagnostics.lastBackendError='" << service.diagnostics().lastBackendError << "' state=" << static_cast<int>(service.state()) << '\n';
    assert(service.configureMappingProfile("paused-remap"));
    assert(service.diagnostics().activeMappingProfile == "paused-remap");
    std::cerr << "DBG: before detach state=" << static_cast<int>(service.state()) << '\n';
    assert(service.detachAdapter(3u));
    std::cerr << "DBG: after detach diagnostics.lastBackendError='" << service.diagnostics().lastBackendError << "' state=" << static_cast<int>(service.state()) << '\n';
    assert(service.state() == BMMQ::InputLifecycleState::Detached);
    assert(service.diagnostics().activeAdapterSummary.empty());
    assert(service.diagnostics().lastCommittedGeneration == 3u);
    assert(!service.detachAdapter(4u));

    auto failingAdapter = std::make_unique<TestInputAdapter>(safeCaps, 0x01u);
    failingAdapter->setOpenSucceeds(false, "open failed");
    assert(service.attachAdapter(std::move(failingAdapter)));
    std::cerr << "DBG: after attach diagnostics.lastBackendError='" << service.diagnostics().lastBackendError << "' state=" << static_cast<int>(service.state()) << '\n';
    assert(!service.resume());
    std::cerr << "DBG: after resume diagnostics.lastBackendError='" << service.diagnostics().lastBackendError << "' state=" << static_cast<int>(service.state()) << '\n';
    assert(service.state() == BMMQ::InputLifecycleState::Faulted);
    ASSERT_DIAG_EQ("open failed", service.diagnostics().lastBackendError);

    return 0;
}

#undef ASSERT_DIAG_EQ