#include "SdlFrontendPluginLoader.hpp"

#include <stdexcept>

#include "DynamicPluginModule.hpp"

namespace BMMQ {

std::filesystem::path defaultSdlFrontendPluginPath(const std::filesystem::path& executablePath)
{
    std::filesystem::path resolved = executablePath.empty()
        ? std::filesystem::current_path() / "timeEmulator"
        : executablePath;
    if (!resolved.is_absolute()) resolved = std::filesystem::absolute(resolved);
    return resolved.parent_path() / kDefaultSdlFrontendPluginFilename;
}

std::unique_ptr<ISdlFrontendPlugin> loadSdlFrontendPlugin(
    const std::filesystem::path& pluginPath,
    const SdlFrontendConfig& config)
{
    auto module = Plugin::DynamicPluginModule::load(pluginPath);
    const auto ids = module.frontendIds();
    if (ids.empty()) {
        throw std::runtime_error("Plugin module '" + pluginPath.string() +
                                 "' does not expose a frontend");
    }
    if (ids.size() != 1u) {
        throw std::runtime_error("Plugin module '" + pluginPath.string() +
                                 "' exposes multiple frontends; select one explicitly");
    }
    return module.createFrontend(ids.front(), config);
}

} // namespace BMMQ
