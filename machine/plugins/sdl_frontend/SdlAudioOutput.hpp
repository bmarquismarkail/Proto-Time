#ifndef BMMQ_SDL_AUDIO_OUTPUT_HPP
#define BMMQ_SDL_AUDIO_OUTPUT_HPP

#include "../AudioOutput.hpp"

#include <string>

namespace BMMQ {

class AudioEngine;

class SdlAudioOutputBackend final : public IAudioOutputBackend {
public:
    SdlAudioOutputBackend() = default;
    ~SdlAudioOutputBackend() override;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool open(AudioEngine& engine, const AudioOutputOpenConfig& config) override;
    void close() noexcept override;
    [[nodiscard]] bool ready() const noexcept override;
    [[nodiscard]] std::string_view lastError() const noexcept override;
    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept override;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

} // namespace BMMQ

#endif // BMMQ_SDL_AUDIO_OUTPUT_HPP
