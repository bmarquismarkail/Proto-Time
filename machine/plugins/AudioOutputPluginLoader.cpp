#include "AudioOutputPluginLoader.hpp"

#include <stdexcept>
#include <string>

#include "DynamicPluginModule.hpp"

namespace BMMQ {

std::string_view normalizeAudioOutputId(std::string_view outputId) noexcept
{
    return outputId == "sdl" ? std::string_view("bmmq.audio-output.sdl") : outputId;
}

std::filesystem::path defaultAudioOutputPluginPath(
    const std::filesystem::path& executablePath, std::string_view pluginFilename)
{
    std::filesystem::path resolved = executablePath.empty()
        ? std::filesystem::current_path() / "timeEmulator" : executablePath;
    if (!resolved.is_absolute()) resolved = std::filesystem::absolute(resolved);
    return resolved.parent_path() / std::filesystem::path(pluginFilename);
}

std::unique_ptr<IAudioOutputBackend> loadAudioOutputPlugin(
    const std::filesystem::path& pluginPath, std::string_view outputId)
{
    auto module = Plugin::DynamicPluginModule::load(pluginPath);
    const auto ids = module.audioOutputIds();
    if (ids.empty()) {
        throw std::runtime_error("Plugin module '" + pluginPath.string() +
                                 "' does not expose an audio output");
    }
    if (outputId.empty()) {
        if (ids.size() != 1u) {
            throw std::runtime_error("Plugin module '" + pluginPath.string() +
                                     "' exposes multiple audio outputs; select one explicitly");
        }
        outputId = ids.front();
    }
    return module.createAudioOutput(normalizeAudioOutputId(outputId));
}

} // namespace BMMQ
