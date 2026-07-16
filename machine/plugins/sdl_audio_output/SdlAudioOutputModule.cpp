#include "machine/plugins/abi/TimePluginAbi.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <new>
#include <string>

#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
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

namespace {

struct SdlAudioOutput {
    TimeAudioOutputHostApiV1 host{};
    std::string lastError;
    std::atomic<std::uint64_t> callbackCount{0u};
    std::atomic<std::uint64_t> serviceCalls{0u};
    std::atomic<std::uint64_t> startCount{0u};
    std::atomic<std::uint64_t> pauseCount{0u};
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    SDL_AudioDeviceID device = 0;
    bool audioSubsystemInitialized = false;
#endif
    bool started = false;
};

void closeOutput(SdlAudioOutput& output) noexcept;

#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
void audioCallback(void* context, Uint8* stream, int bytes)
{
    if (context == nullptr || stream == nullptr || bytes <= 0) return;
    auto& output = *static_cast<SdlAudioOutput*>(context);
    const auto requested = static_cast<std::uint32_t>(
        static_cast<unsigned int>(bytes) / sizeof(std::int16_t));
    if (output.host.drain_ready_audio == nullptr || requested == 0u) {
        std::fill_n(stream, static_cast<std::size_t>(bytes), Uint8{0});
        return;
    }
    output.callbackCount.fetch_add(1u, std::memory_order_relaxed);
    (void)output.host.drain_ready_audio(
        output.host.host_context, reinterpret_cast<std::int16_t*>(stream), requested);
}
#endif

void* createOutput(const TimeAudioOutputHostApiV1* host) noexcept
{
    if (host == nullptr || host->struct_size < sizeof(TimeAudioOutputHostApiV1) ||
        host->abi_version != TIME_PLUGIN_ABI_VERSION_V1 ||
        host->drain_ready_audio == nullptr) return nullptr;
    auto* output = new (std::nothrow) SdlAudioOutput;
    if (output != nullptr) output->host = *host;
    return output;
}

void destroyOutput(void* instance) noexcept
{
    auto* output = static_cast<SdlAudioOutput*>(instance);
    if (output == nullptr) return;
    closeOutput(*output);
    output->host.host_context = nullptr;
    output->host.drain_ready_audio = nullptr;
    delete output;
}

int32_t openOutput(void* instance, const TimeAudioOutputConfigV1* config,
                   TimeAudioOutputDeviceInfoV1* obtained) noexcept
{
    auto* output = static_cast<SdlAudioOutput*>(instance);
    if (output == nullptr || config == nullptr || obtained == nullptr ||
        config->struct_size < sizeof(TimeAudioOutputConfigV1) ||
        obtained->struct_size < sizeof(TimeAudioOutputDeviceInfoV1) ||
        config->requested_sample_rate == 0u || config->requested_channels == 0u ||
        config->requested_channels > 2u || config->callback_samples == 0u) return 0;
    closeOutput(*output);
    output->lastError.clear();
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        output->lastError = SDL_GetError();
        return 0;
    }
    output->audioSubsystemInitialized = true;
    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(std::min<std::uint32_t>(
        config->requested_sample_rate, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(config->requested_channels);
    const auto frames = std::max<std::uint32_t>(
        1u, config->callback_samples / config->requested_channels);
    desired.samples = static_cast<Uint16>(std::min<std::uint32_t>(
        frames, std::numeric_limits<Uint16>::max()));
    desired.callback = &audioCallback;
    desired.userdata = output;
    SDL_AudioSpec actual{};
    output->device = SDL_OpenAudioDevice(nullptr, 0, &desired, &actual, 0);
    if (output->device == 0) {
        output->lastError = SDL_GetError();
        closeOutput(*output);
        return 0;
    }
    if (actual.format != AUDIO_S16SYS || actual.channels == 0u || actual.channels > 2u) {
        output->lastError = "SDL audio device format mismatch";
        closeOutput(*output);
        return 0;
    }
    obtained->sample_rate = actual.freq > 0 ? static_cast<std::uint32_t>(actual.freq)
                                             : config->requested_sample_rate;
    obtained->channels = actual.channels;
    obtained->callback_samples = static_cast<std::uint32_t>(
        actual.samples != 0u ? actual.samples : desired.samples) * obtained->channels;
    output->started = false;
    return 1;
#else
    output->lastError = "SDL audio module was built without SDL2 support";
    return 0;
#endif
}

int32_t startOutput(void* instance) noexcept
{
    auto* output = static_cast<SdlAudioOutput*>(instance);
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    if (output == nullptr || output->device == 0) return 0;
    SDL_PauseAudioDevice(output->device, 0);
    output->started = true;
    output->startCount.fetch_add(1u, std::memory_order_relaxed);
    return 1;
#else
    (void)output;
    return 0;
#endif
}

void pauseOutput(void* instance) noexcept
{
    auto* output = static_cast<SdlAudioOutput*>(instance);
    if (output == nullptr) return;
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    if (output->device != 0) SDL_PauseAudioDevice(output->device, 1);
#endif
    if (output->started) output->pauseCount.fetch_add(1u, std::memory_order_relaxed);
    output->started = false;
}

int32_t serviceOutput(void* instance) noexcept
{
    auto* output = static_cast<SdlAudioOutput*>(instance);
    if (output == nullptr) return 0;
    output->serviceCalls.fetch_add(1u, std::memory_order_relaxed);
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    return output->device != 0 ? 1 : 0;
#else
    return 0;
#endif
}

void closeOutput(SdlAudioOutput& output) noexcept
{
    pauseOutput(&output);
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    if (output.device != 0) {
        SDL_CloseAudioDevice(output.device);
        output.device = 0;
    }
    if (output.audioSubsystemInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        output.audioSubsystemInitialized = false;
    }
#endif
}

void closeOutputApi(void* instance) noexcept
{
    auto* output = static_cast<SdlAudioOutput*>(instance);
    if (output != nullptr) closeOutput(*output);
}

const char* backendName(const void*) noexcept { return "SDL2 audio output module"; }
const char* lastError(const void* instance) noexcept
{
    const auto* output = static_cast<const SdlAudioOutput*>(instance);
    return output != nullptr ? output->lastError.c_str() : "invalid SDL audio instance";
}

int32_t queryStats(const void* instance, TimeAudioOutputStatsV1* stats) noexcept
{
    const auto* output = static_cast<const SdlAudioOutput*>(instance);
    if (output == nullptr || stats == nullptr ||
        stats->struct_size < sizeof(TimeAudioOutputStatsV1)) return 0;
    stats->callback_count = output->callbackCount.load(std::memory_order_relaxed);
    stats->service_calls = output->serviceCalls.load(std::memory_order_relaxed);
    stats->start_count = output->startCount.load(std::memory_order_relaxed);
    stats->pause_count = output->pauseCount.load(std::memory_order_relaxed);
#if BMMQ_SDL_AUDIO_COMPILED_WITH_SDL
    stats->device_open = output->device != 0 ? 1 : 0;
#else
    stats->device_open = 0;
#endif
    stats->device_started = output->started ? 1 : 0;
    return 1;
}

const TimeAudioOutputApiV1 kApi{
    sizeof(TimeAudioOutputApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    &createOutput, &destroyOutput, &openOutput, &startOutput, &pauseOutput,
    &serviceOutput, &closeOutputApi, &backendName, &lastError, &queryStats};
const TimePluginDescriptorV1 kDescriptor{
    sizeof(TimePluginDescriptorV1), TIME_PLUGIN_KIND_AUDIO_OUTPUT_V1,
    "bmmq.audio-output.sdl", "SDL2 Audio Output", &kApi, sizeof(TimeAudioOutputApiV1)};

const TimePluginDescriptorV1* pluginAt(std::uint32_t index) noexcept
{
    return index == 0u ? &kDescriptor : nullptr;
}

const TimePluginModuleV1 kModule{
    sizeof(TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "bmmq.module.sdl-audio-output", "Proto-Time SDL2 Audio Output Module", 1u, &pluginAt};

} // namespace

extern "C" TIME_PLUGIN_EXPORT const TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &kModule;
}
