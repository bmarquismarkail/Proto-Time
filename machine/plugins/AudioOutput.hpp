#ifndef BMMQ_AUDIO_OUTPUT_HPP
#define BMMQ_AUDIO_OUTPUT_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace BMMQ {

class AudioEngine;
class AudioService;

enum class AudioOutputErrorCode {
    None = 0,
    InvalidConfig,
    InvalidPath,
    PermissionDenied,
    DiskFull,
    UnsupportedConfig,
    WriteFailed,
    BackendUnavailable,
    DeviceOpenFailed,
};

struct AudioOutputOpenConfig {
    std::string backend = "sdl";
    int requestedSampleRate = 48000;
    std::size_t callbackChunkSamples = 256;
    int channels = 1;
    int testForcedDeviceSampleRate = 0;
    std::filesystem::path filePath;
    bool appendToFile = false;
    AudioService* audioService = nullptr;
};

struct AudioOutputDeviceInfo {
    int sampleRate = 0;
    std::size_t callbackChunkSamples = 0;
    int channels = 1;
};

class IAudioOutputBackend {
public:
    virtual ~IAudioOutputBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual bool open(AudioEngine& engine, const AudioOutputOpenConfig& config) = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool ready() const noexcept = 0;
    [[nodiscard]] virtual std::string_view lastError() const noexcept = 0;
    [[nodiscard]] virtual AudioOutputErrorCode lastErrorCode() const noexcept = 0;
    [[nodiscard]] virtual AudioOutputDeviceInfo deviceInfo() const noexcept = 0;
};

} // namespace BMMQ

#endif // BMMQ_AUDIO_OUTPUT_HPP
