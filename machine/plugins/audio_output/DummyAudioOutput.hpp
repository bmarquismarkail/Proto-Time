#ifndef BMMQ_DUMMY_AUDIO_OUTPUT_HPP
#define BMMQ_DUMMY_AUDIO_OUTPUT_HPP

#include "../AudioOutput.hpp"

#include <memory>

namespace BMMQ {

class AudioEngine;

class DummyAudioOutputBackend final : public IAudioOutputBackend {
public:
    DummyAudioOutputBackend();
    ~DummyAudioOutputBackend() override;
    DummyAudioOutputBackend(const DummyAudioOutputBackend&) = delete;
    DummyAudioOutputBackend& operator=(const DummyAudioOutputBackend&) = delete;
    DummyAudioOutputBackend(DummyAudioOutputBackend&&) = delete;
    DummyAudioOutputBackend& operator=(DummyAudioOutputBackend&&) = delete;

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

#endif // BMMQ_DUMMY_AUDIO_OUTPUT_HPP
