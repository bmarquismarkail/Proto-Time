#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "machine/InputService.hpp"

#define CHECK_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "check failed: " << #expr << '\n'; \
            return 1; \
        } \
    } while (false)

#define ASSERT_DIAG_EQ(expected, actualExpr) \
    do { \
        const std::string expectedValue = (expected); \
        const auto actualValue = (actualExpr); \
        if (actualValue != expectedValue) { \
            std::cerr << "expected diagnostic '" << expectedValue \
                      << "' but got '" << actualValue << "'" << '\n'; \
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
    BMMQ::InputService service({
        .stagingCapacity = 2,
    });

    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Detached);
    CHECK_TRUE(service.configureMappingProfile("default-gameboy"));
    CHECK_TRUE(service.diagnostics().activeMappingProfile == "default-gameboy");

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
    CHECK_TRUE(service.attachAdapter(std::move(safeAdapter)));
    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Paused);
    CHECK_TRUE(service.resume());
    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Active);
    CHECK_TRUE(service.pollActiveAdapter(1u));
    CHECK_TRUE(service.committedDigitalMask().has_value());
    CHECK_TRUE(*service.committedDigitalMask() == 0x10u);
    CHECK_TRUE(service.committedAnalogState().has_value());
    CHECK_TRUE(service.committedAnalogState()->channels[1] == 77);
    CHECK_TRUE(service.diagnostics().lastCommittedGeneration == 1u);
    CHECK_TRUE(!service.configureMappingProfile("active-remap"));

    auto unsafeCaps = safeCaps;
    unsafeCaps.pollingSafe = false;
    auto unsafeAdapter = std::make_unique<TestInputAdapter>(unsafeCaps, 0x20u);
    CHECK_TRUE(!service.attachAdapter(std::move(unsafeAdapter)));
    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Active);

    service.recordPollFailure("temporary poll failure");
    CHECK_TRUE(service.diagnostics().pollFailureCount == 1u);
    CHECK_TRUE(service.committedDigitalMask().has_value());
    CHECK_TRUE(*service.committedDigitalMask() == 0x10u);

    service.recordBackendLoss("input backend detached", 2u);
    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Faulted);
    CHECK_TRUE(service.diagnostics().neutralFallbackCount == 1u);
    CHECK_TRUE(service.committedDigitalMask().has_value());
    CHECK_TRUE(*service.committedDigitalMask() == 0u);

    CHECK_TRUE(service.pause());
    CHECK_TRUE(service.configureMappingProfile("paused-remap"));
    CHECK_TRUE(service.diagnostics().activeMappingProfile == "paused-remap");
    CHECK_TRUE(service.detachAdapter(3u));
    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Detached);
    CHECK_TRUE(service.diagnostics().activeAdapterSummary.empty());
    CHECK_TRUE(service.diagnostics().lastCommittedGeneration == 3u);
    CHECK_TRUE(!service.detachAdapter(4u));

    auto failingAdapter = std::make_unique<TestInputAdapter>(safeCaps, 0x01u);
    failingAdapter->setOpenSucceeds(false, "open failed");
    CHECK_TRUE(service.attachAdapter(std::move(failingAdapter)));
    CHECK_TRUE(!service.resume());
    CHECK_TRUE(service.state() == BMMQ::InputLifecycleState::Faulted);
    ASSERT_DIAG_EQ("open failed", service.diagnostics().lastBackendError);

    return 0;
}

#undef ASSERT_DIAG_EQ
#undef CHECK_TRUE