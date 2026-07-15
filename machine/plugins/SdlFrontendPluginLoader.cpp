#include "SdlFrontendPluginLoader.hpp"

namespace BMMQ {

std::filesystem::path defaultSdlFrontendPluginPath(const std::filesystem::path& executablePath)
{
    return defaultFrontendPluginPath(executablePath, kDefaultSdlFrontendPluginFilename);
}

std::unique_ptr<ISdlFrontendPlugin> loadSdlFrontendPlugin(
    const std::filesystem::path& pluginPath,
    const SdlFrontendConfig& config)
{
    return loadFrontendPlugin(pluginPath, config);
}

} // namespace BMMQ
