#ifndef BMMQ_FRONTEND_PLUGIN_LOADER_HPP
#define BMMQ_FRONTEND_PLUGIN_LOADER_HPP

#include <filesystem>
#include <memory>
#include <string_view>

#include "FrontendPlugin.hpp"

namespace BMMQ {

inline constexpr const char* kDefaultSdlFrontendPluginFilename =
    "libtime-sdl-frontend-plugin.so";
inline constexpr const char* kDefaultGlfwFrontendPluginFilename =
    "libtime-glfw-frontend-plugin.so";

[[nodiscard]] std::string_view normalizeFrontendId(std::string_view frontendId) noexcept;
[[nodiscard]] std::string_view defaultFrontendPluginFilename(
    std::string_view frontendId) noexcept;

[[nodiscard]] std::filesystem::path defaultFrontendPluginPath(
    const std::filesystem::path& executablePath,
    std::string_view pluginFilename);

[[nodiscard]] std::unique_ptr<IFrontendPlugin> loadFrontendPlugin(
    const std::filesystem::path& pluginPath,
    const FrontendConfig& config,
    std::string_view frontendId = {});

} // namespace BMMQ

#endif // BMMQ_FRONTEND_PLUGIN_LOADER_HPP
