#ifndef BMMQ_AUDIO_OUTPUT_PLUGIN_LOADER_HPP
#define BMMQ_AUDIO_OUTPUT_PLUGIN_LOADER_HPP

#include <filesystem>
#include <memory>
#include <string_view>

#include "AudioOutput.hpp"

namespace BMMQ {

inline constexpr const char* kDefaultSdlAudioOutputPluginFilename =
    "libtime-sdl-audio-output-plugin.so";

[[nodiscard]] std::string_view normalizeAudioOutputId(std::string_view outputId) noexcept;
[[nodiscard]] std::filesystem::path defaultAudioOutputPluginPath(
    const std::filesystem::path& executablePath,
    std::string_view pluginFilename = kDefaultSdlAudioOutputPluginFilename);
[[nodiscard]] std::unique_ptr<IAudioOutputBackend> loadAudioOutputPlugin(
    const std::filesystem::path& pluginPath,
    std::string_view outputId = {});

} // namespace BMMQ

#endif // BMMQ_AUDIO_OUTPUT_PLUGIN_LOADER_HPP
