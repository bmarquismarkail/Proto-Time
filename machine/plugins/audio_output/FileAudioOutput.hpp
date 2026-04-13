#ifndef BMMQ_FILE_AUDIO_OUTPUT_HPP
#define BMMQ_FILE_AUDIO_OUTPUT_HPP

#include "../AudioOutput.hpp"

#include <memory>
#include <string>

namespace BMMQ {

class AudioEngine;

class FileAudioOutputBackend final : public IAudioOutputBackend {
public:
    FileAudioOutputBackend() = default;
    ~FileAudioOutputBackend() override;
    FileAudioOutputBackend(const FileAudioOutputBackend&) = delete;
    FileAudioOutputBackend& operator=(const FileAudioOutputBackend&) = delete;
    FileAudioOutputBackend(FileAudioOutputBackend&&) = delete;
    FileAudioOutputBackend& operator=(FileAudioOutputBackend&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool open(AudioEngine& engine, const AudioOutputOpenConfig& config) override;
    void close() noexcept override;
    [[nodiscard]] bool ready() const noexcept override;
    [[nodiscard]] std::string_view lastError() const noexcept override;
    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace BMMQ

#endif // BMMQ_FILE_AUDIO_OUTPUT_HPP
