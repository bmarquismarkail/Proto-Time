#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

#include "machine/InputService.hpp"

#define SMOKE_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "smoke_input_fallback check failed: " #condition << '\n'; \
            return 1; \
        } \
    } while (false)

namespace {

class FallbackAdapter final : public BMMQ::IDigitalInputSourcePlugin {
public:
    explicit FallbackAdapter(BMMQ::InputButtonMask mask)
        : mask_(mask) {}

    [[nodiscard]] BMMQ::InputPluginCapabilities capabilities() const noexcept override
    {
        return {
            .pollingSafe = true,
            .eventPumpSafe = false,
            .deterministic = true,
            .supportsDigital = true,
            .supportsAnalog = false,
            .fixedLogicalLayout = true,
            .hotSwapSafe = false,
            .liveSeek = false,
            .nonRealtimeOnly = false,
            .headlessSafe = true,
        };
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "fallback-adapter";
    }

    [[nodiscard]] bool open() override
    {
        return true;
    }

    void close() noexcept override {}

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return {};
    }

    [[nodiscard]] std::optional<BMMQ::InputButtonMask> sampleDigitalInput() override
    {
        return mask_;
    }

private:
    BMMQ::InputButtonMask mask_;
};

} // namespace

int main()
{
    BMMQ::InputService service({
        .stagingCapacity = 2,
    });

    SMOKE_CHECK(service.attachAdapter(std::make_unique<FallbackAdapter>(0x20u)));
    SMOKE_CHECK(service.resume());
    SMOKE_CHECK(service.pollActiveAdapter(1u));
    SMOKE_CHECK(service.committedDigitalMask().has_value());
    SMOKE_CHECK(*service.committedDigitalMask() == 0x20u);

    service.recordPollFailure("temporary input failure");
    SMOKE_CHECK(service.diagnostics().pollFailureCount == 1u);
    SMOKE_CHECK(service.committedDigitalMask().has_value());
    SMOKE_CHECK(*service.committedDigitalMask() == 0x20u);
    SMOKE_CHECK(service.diagnostics().lastBackendError == "temporary input failure");

    service.recordBackendLoss("backend disconnected", 2u);
    SMOKE_CHECK(service.state() == BMMQ::InputLifecycleState::Faulted);
    SMOKE_CHECK(service.diagnostics().neutralFallbackCount == 1u);
    SMOKE_CHECK(service.committedDigitalMask().has_value());
    SMOKE_CHECK(*service.committedDigitalMask() == 0u);
    SMOKE_CHECK(service.diagnostics().lastBackendError == "backend disconnected");

    return 0;
}

#undef SMOKE_CHECK