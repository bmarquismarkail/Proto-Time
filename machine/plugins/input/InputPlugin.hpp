#ifndef BMMQ_INPUT_PLUGIN_HPP
#define BMMQ_INPUT_PLUGIN_HPP

#include <optional>
#include <string_view>

#include "InputCapabilities.hpp"
#include "InputEngine.hpp"

namespace BMMQ {

class IInputPlugin {
public:
    virtual ~IInputPlugin() = default;

    [[nodiscard]] virtual InputPluginCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    // Return false for expected/recoverable open failures; reserve exceptions for programming or irrecoverable errors.
    [[nodiscard]] virtual bool open() = 0;
    virtual void close() noexcept = 0;
    // Returns a human-readable description of the most recent open() failure for caller diagnostics.
    [[nodiscard]] virtual std::string_view lastError() const noexcept = 0;
};

class IDigitalInputSourcePlugin : public virtual IInputPlugin {
public:
    ~IDigitalInputSourcePlugin() override = default;

    [[nodiscard]] virtual std::optional<InputButtonMask> sampleDigitalInput() = 0;
};

class IAnalogInputSourcePlugin : public virtual IInputPlugin {
public:
    ~IAnalogInputSourcePlugin() override = default;

    [[nodiscard]] virtual std::optional<InputAnalogState> sampleAnalogInput() = 0;
};

} // namespace BMMQ

#endif // BMMQ_INPUT_PLUGIN_HPP