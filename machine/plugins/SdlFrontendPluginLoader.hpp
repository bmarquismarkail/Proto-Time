#ifndef BMMQ_SDL_FRONTEND_PLUGIN_LOADER_HPP
#define BMMQ_SDL_FRONTEND_PLUGIN_LOADER_HPP

#include <filesystem>
#include <memory>

#include "FrontendPluginLoader.hpp"

namespace BMMQ {

[[nodiscard]] std::filesystem::path defaultSdlFrontendPluginPath(const std::filesystem::path& executablePath);

[[nodiscard]] std::unique_ptr<ISdlFrontendPlugin> loadSdlFrontendPlugin(
    const std::filesystem::path& pluginPath,
    const SdlFrontendConfig& config);

} // namespace BMMQ

#endif // BMMQ_SDL_FRONTEND_PLUGIN_LOADER_HPP
