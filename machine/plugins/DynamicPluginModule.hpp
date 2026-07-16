#ifndef BMMQ_DYNAMIC_PLUGIN_MODULE_HPP
#define BMMQ_DYNAMIC_PLUGIN_MODULE_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/plugins/AudioOutput.hpp"
#include "machine/plugins/FrontendPlugin.hpp"

namespace BMMQ::Plugin {

class DynamicPluginModule {
public:
    struct State; // opaque implementation shared with policy adapters
    DynamicPluginModule() = default;

    [[nodiscard]] static DynamicPluginModule load(const std::filesystem::path& path);
    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view displayName() const noexcept;
    [[nodiscard]] std::vector<std::string> executorPolicyIds() const;
    [[nodiscard]] std::unique_ptr<IExecutorPolicyPlugin> createExecutorPolicy(
        std::string_view id) const;
    [[nodiscard]] std::vector<std::string> frontendIds() const;
    [[nodiscard]] std::unique_ptr<IFrontendPlugin> createFrontend(
        std::string_view id, const FrontendConfig& config) const;
    [[nodiscard]] std::vector<std::string> audioOutputIds() const;
    [[nodiscard]] std::unique_ptr<IAudioOutputBackend> createAudioOutput(
        std::string_view id) const;

private:
    explicit DynamicPluginModule(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

} // namespace BMMQ::Plugin

#endif // BMMQ_DYNAMIC_PLUGIN_MODULE_HPP
