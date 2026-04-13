#include "SdlAudioOutput.hpp"

#include "../../AudioService.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
#  if defined(__has_include)
#    if __has_include(<SDL2/SDL.h>)
#      include <SDL2/SDL.h>
#    else
#      include <SDL.h>
#    endif
#  else
#    include <SDL.h>
#  endif
#endif

namespace BMMQ {

class SdlAudioOutputBackend::Impl {
public:
    [[nodiscard]] std::string_view name() const noexcept
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        return "SDL2 audio output";
#else
        return "SDL2 audio unavailable";
#endif
    }

    [[nodiscard]] bool open(AudioEngine& engine, const AudioOutputOpenConfig& config)
    {
        close();
        lastError_.clear();
        lastErrorCode_ = AudioOutputErrorCode::None;

#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (config.audioService == nullptr) {
            lastError_ = "Audio service is required";
            lastErrorCode_ = AudioOutputErrorCode::InvalidConfig;
            return false;
        }
        if (config.channels != 1) {
            lastError_ = "Only mono output is supported";
            lastErrorCode_ = AudioOutputErrorCode::UnsupportedConfig;
            return false;
        }
        service_ = config.audioService;
        engine_ = &engine;

        SDL_AudioSpec desired{};
        desired.freq = std::max(config.requestedSampleRate, 1);
        desired.format = AUDIO_S16SYS;
        desired.channels = static_cast<Uint8>(config.channels);
        desired.samples = static_cast<Uint16>(std::max<std::size_t>(config.callbackChunkSamples, 1u));
        desired.callback = &Impl::sdlAudioCallback;
        desired.userdata = this;

        SDL_AudioSpec obtained{};
        audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if (audioDevice_ == 0) {
            lastError_ = SDL_GetError();
            lastErrorCode_ = AudioOutputErrorCode::DeviceOpenFailed;
            engine_ = nullptr;
            return false;
        }

        if (obtained.format != AUDIO_S16SYS || obtained.channels != 1) {
            lastError_ = "SDL audio device format mismatch";
            lastErrorCode_ = AudioOutputErrorCode::UnsupportedConfig;
            close();
            return false;
        }

        deviceInfo_.sampleRate = obtained.freq > 0 ? obtained.freq : desired.freq;
        if (config.testForcedDeviceSampleRate > 0) {
            deviceInfo_.sampleRate = config.testForcedDeviceSampleRate;
        }
        deviceInfo_.callbackChunkSamples = obtained.samples != 0 ? obtained.samples : desired.samples;
        deviceInfo_.channels = obtained.channels != 0 ? obtained.channels : desired.channels;

        engine_->setDeviceSampleRate(deviceInfo_.sampleRate);
        SDL_PauseAudioDevice(audioDevice_, 0);
        return true;
#else
        (void)engine;
        (void)config;
        lastError_ = "SDL audio backend unavailable";
        lastErrorCode_ = AudioOutputErrorCode::BackendUnavailable;
        return false;
#endif
    }

    void close() noexcept
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        if (audioDevice_ != 0) {
            SDL_CloseAudioDevice(audioDevice_);
            audioDevice_ = 0;
        }
#endif
        engine_ = nullptr;
        service_ = nullptr;
        deviceInfo_ = {};
    }

    [[nodiscard]] bool ready() const noexcept
    {
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
        return audioDevice_ != 0;
#else
        return false;
#endif
    }

    [[nodiscard]] std::string_view lastError() const noexcept
    {
        return lastError_;
    }

    [[nodiscard]] AudioOutputErrorCode lastErrorCode() const noexcept
    {
        return lastErrorCode_;
    }

    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept
    {
        return deviceInfo_;
    }

private:
#if BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL
    static void sdlAudioCallback(void* userdata, Uint8* stream, int len)
    {
        if (userdata == nullptr || stream == nullptr || len <= 0) {
            return;
        }

        static_cast<Impl*>(userdata)->drainAudioCallback(stream, len);
    }

    void drainAudioCallback(Uint8* stream, int len) noexcept
    {
        if (engine_ == nullptr || service_ == nullptr) {
            return;
        }

        auto* out = reinterpret_cast<int16_t*>(stream);
        const auto requestedSamples = static_cast<std::size_t>(len / static_cast<int>(sizeof(int16_t)));
        service_->renderForOutput(std::span<int16_t>(out, requestedSamples));
    }

    SDL_AudioDeviceID audioDevice_ = 0;
#endif
    AudioEngine* engine_ = nullptr;
    AudioService* service_ = nullptr;
    AudioOutputDeviceInfo deviceInfo_{};
    std::string lastError_;
    AudioOutputErrorCode lastErrorCode_ = AudioOutputErrorCode::None;
};

SdlAudioOutputBackend::SdlAudioOutputBackend() = default;
SdlAudioOutputBackend::~SdlAudioOutputBackend() = default;

std::string_view SdlAudioOutputBackend::name() const noexcept
{
    return impl_ != nullptr ? impl_->name() : "SDL2 audio output";
}

bool SdlAudioOutputBackend::open(AudioEngine& engine, const AudioOutputOpenConfig& config)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->open(engine, config);
}

void SdlAudioOutputBackend::close() noexcept
{
    if (impl_ != nullptr) {
        impl_->close();
    }
}

bool SdlAudioOutputBackend::ready() const noexcept
{
    return impl_ != nullptr && impl_->ready();
}

std::string_view SdlAudioOutputBackend::lastError() const noexcept
{
    return impl_ != nullptr ? impl_->lastError() : std::string_view{};
}

AudioOutputErrorCode SdlAudioOutputBackend::lastErrorCode() const noexcept
{
    return impl_ != nullptr ? impl_->lastErrorCode() : AudioOutputErrorCode::None;
}

AudioOutputDeviceInfo SdlAudioOutputBackend::deviceInfo() const noexcept
{
    return impl_ != nullptr ? impl_->deviceInfo() : AudioOutputDeviceInfo{};
}

} // namespace BMMQ
