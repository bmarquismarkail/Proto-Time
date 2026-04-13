#ifndef BMMQ_SDL_AUDIO_OUTPUT_HPP
#define BMMQ_SDL_AUDIO_OUTPUT_HPP

#include "../AudioOutput.hpp"

#include <memory>
#include <string>

namespace BMMQ {

class AudioEngine;

class SdlAudioOutputBackend final : public IAudioOutputBackend {
public:
    SdlAudioOutputBackend();
    ~SdlAudioOutputBackend() override;
    SdlAudioOutputBackend(const SdlAudioOutputBackend&) = delete;
    SdlAudioOutputBackend& operator=(const SdlAudioOutputBackend&) = delete;
    SdlAudioOutputBackend(SdlAudioOutputBackend&&) = delete;
    SdlAudioOutputBackend& operator=(SdlAudioOutputBackend&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool open(AudioEngine& engine, const AudioOutputOpenConfig& config) override;
    void close() noexcept override;
    [[nodiscard]] bool ready() const noexcept override;
    [[nodiscard]] std::string_view lastError() const noexcept override;
    [[nodiscard]] AudioOutputErrorCode lastErrorCode() const noexcept override;
    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace BMMQ

#endif // BMMQ_SDL_AUDIO_OUTPUT_HPP
