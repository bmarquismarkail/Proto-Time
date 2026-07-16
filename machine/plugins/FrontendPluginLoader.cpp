#include "FrontendPluginLoader.hpp"

#include <stdexcept>
#include <string>

#include "DynamicPluginModule.hpp"

namespace BMMQ {

std::string_view normalizeFrontendId(std::string_view frontendId) noexcept
{
    if (frontendId == "sdl") return "bmmq.frontend.sdl";
    if (frontendId == "glfw") return "bmmq.frontend.glfw";
    return frontendId;
}

std::string_view defaultFrontendPluginFilename(std::string_view frontendId) noexcept
{
    return normalizeFrontendId(frontendId) == "bmmq.frontend.glfw"
        ? std::string_view(kDefaultGlfwFrontendPluginFilename)
        : std::string_view(kDefaultSdlFrontendPluginFilename);
}

std::filesystem::path defaultFrontendPluginPath(
    const std::filesystem::path& executablePath,
    std::string_view pluginFilename)
{
    std::filesystem::path resolved = executablePath.empty()
        ? std::filesystem::current_path() / "timeEmulator"
        : executablePath;
    if (!resolved.is_absolute()) resolved = std::filesystem::absolute(resolved);
    return resolved.parent_path() / std::filesystem::path(pluginFilename);
}

std::unique_ptr<IFrontendPlugin> loadFrontendPlugin(
    const std::filesystem::path& pluginPath,
    const FrontendConfig& config,
    std::string_view frontendId)
{
    auto module = Plugin::DynamicPluginModule::load(pluginPath);
    const auto ids = module.frontendIds();
    if (ids.empty()) {
        throw std::runtime_error("Plugin module '" + pluginPath.string() +
                                 "' does not expose a frontend");
    }
    if (frontendId.empty()) {
        if (ids.size() != 1u) {
            throw std::runtime_error("Plugin module '" + pluginPath.string() +
                                     "' exposes multiple frontends; select one explicitly");
        }
        frontendId = ids.front();
    }
    return module.createFrontend(normalizeFrontendId(frontendId), config);
}

} // namespace BMMQ
