#ifndef BMMQ_SDL_FRONTEND_PLUGIN_HPP
#define BMMQ_SDL_FRONTEND_PLUGIN_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../InputTypes.hpp"
#include "IoPlugin.hpp"

namespace BMMQ {

struct SdlFrontendConfig {
    std::string windowTitle = "T.I.M.E. SDL Frontend";
    int windowScale = 2;
    int frameWidth = 160;
    int frameHeight = 144;
    int audioPreviewSampleCount = 128;
    int audioCallbackChunkSamples = 256;
    int testForcedAudioDeviceSampleRate = 0;
    std::size_t audioRingBufferCapacitySamples = 2048;
    bool enableAudioResamplingDiagnostics = false;
    bool enableVideo = true;
    bool enableAudio = true;
    bool enableInput = true;
    bool autoInitializeBackend = false;
    bool createHiddenWindowOnInitialize = false;
    bool pumpBackendEventsOnInputSample = true;
    bool autoPresentOnVideoEvent = true;
    bool showWindowOnPresent = false;
};

struct SdlFrontendStats {
    std::size_t attachCount = 0;
    std::size_t detachCount = 0;
    std::size_t videoEvents = 0;
    std::size_t audioEvents = 0;
    std::size_t inputEvents = 0;
    std::size_t inputPolls = 0;
    std::size_t inputSamplesProvided = 0;
    std::size_t framesPrepared = 0;
    std::size_t framesPresented = 0;
    std::size_t renderAttempts = 0;
    std::size_t audioPreviewsBuilt = 0;
    int audioSourceSampleRate = 48000;
    int audioDeviceSampleRate = 0;
    std::size_t audioCallbackChunkSamples = 0;
    std::size_t audioRingBufferCapacitySamples = 0;
    std::size_t audioBufferedHighWaterSamples = 0;
    std::size_t audioCallbackCount = 0;
    std::size_t audioSamplesDelivered = 0;
    std::size_t audioUnderrunCount = 0;
    std::size_t audioSilenceSamplesFilled = 0;
    std::size_t audioOverrunDropCount = 0;
    std::size_t audioDroppedSamples = 0;
    bool audioResamplingActive = false;
    double audioResampleRatio = 1.0;
    std::size_t audioSourceSamplesPushed = 0;
    std::size_t audioResampleSourceSamplesConsumed = 0;
    std::size_t audioResampleOutputSamplesProduced = 0;
    std::size_t audioQueueWrites = 0;
    std::size_t audioSamplesQueued = 0;
    std::size_t audioQueueLowWaterHits = 0;
    std::size_t audioQueueHighWaterSkips = 0;
    std::size_t audioQueueRecoveryClears = 0;
    std::uint32_t lastQueuedAudioBytes = 0;
    std::uint32_t peakQueuedAudioBytes = 0;
    std::size_t backendInitAttempts = 0;
    std::size_t buttonTransitions = 0;
    std::size_t quitRequests = 0;
    std::size_t hostEventsHandled = 0;
    std::size_t keyEventsHandled = 0;
    std::size_t eventPumpCalls = 0;
    std::size_t backendEventsTranslated = 0;
    std::size_t serviceCalls = 0;
};

enum class SdlFrontendHostEventType : uint8_t {
    None = 0,
    KeyDown = 1,
    KeyUp = 2,
    Quit = 3,
};

enum class SdlFrontendHostKey : uint8_t {
    Unknown = 0,
    Right,
    Left,
    Up,
    Down,
    Z,
    X,
    Backspace,
    Return,
};

struct SdlFrontendHostEvent {
    SdlFrontendHostEventType type = SdlFrontendHostEventType::None;
    SdlFrontendHostKey key = SdlFrontendHostKey::Unknown;
    bool repeat = false;
};

struct SdlFrameBuffer {
    int width = 160;
    int height = 144;
    std::vector<uint32_t> pixels;

    [[nodiscard]] bool empty() const noexcept
    {
        return pixels.empty();
    }

    [[nodiscard]] std::size_t pixelCount() const noexcept
    {
        return pixels.size();
    }
};

struct SdlAudioPreviewBuffer {
    int sampleRate = 48000;
    int channels = 1;
    std::vector<int16_t> samples;

    [[nodiscard]] bool empty() const noexcept
    {
        return samples.empty();
    }

    [[nodiscard]] std::size_t sampleCount() const noexcept
    {
        return samples.size();
    }
};

inline constexpr std::string_view kSdlFrontendPluginId = "bmmq.frontend.sdl";
inline constexpr std::string_view kSdlFrontendPluginDisplayName = "SDL Frontend Plugin";
inline constexpr std::uint32_t kSdlFrontendPluginApiVersion = 1u;
inline constexpr const char* kSdlFrontendPluginApiEntryPoint = "bmmq_get_sdl_frontend_plugin_api_v1";

class ISdlFrontendPlugin : public IVideoPlugin,
                           public IAudioPlugin,
                           public IDigitalInputPlugin {
public:
    ~ISdlFrontendPlugin() override = default;

    std::string_view id() const override
    {
        return kSdlFrontendPluginId;
    }

    std::string_view displayName() const override
    {
        return kSdlFrontendPluginDisplayName;
    }

    [[nodiscard]] virtual const SdlFrontendConfig& config() const noexcept = 0;
    [[nodiscard]] virtual const SdlFrontendStats& stats() const noexcept = 0;
    [[nodiscard]] virtual const std::vector<std::string>& diagnostics() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<VideoStateView>& lastVideoState() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<AudioStateView>& lastAudioState() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<SdlAudioPreviewBuffer>& lastAudioPreview() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<DigitalInputStateView>& lastInputState() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<SdlFrameBuffer>& lastFrame() const noexcept = 0;
    [[nodiscard]] virtual std::string_view lastRenderSummary() const noexcept = 0;
    [[nodiscard]] virtual bool windowVisible() const noexcept = 0;
    [[nodiscard]] virtual bool windowVisibilityRequested() const noexcept = 0;
    virtual void requestWindowVisibility(bool visible) = 0;
    virtual bool serviceFrontend() = 0;
    virtual void setQueuedDigitalInputMask(uint32_t pressedMask) = 0;
    virtual void clearQueuedDigitalInputMask() = 0;
    [[nodiscard]] virtual std::optional<uint32_t> queuedDigitalInputMask() const noexcept = 0;
    virtual void pressButton(InputButton button) = 0;
    virtual void releaseButton(InputButton button) = 0;
    [[nodiscard]] virtual bool isButtonPressed(InputButton button) const noexcept = 0;
    virtual void clearQuitRequest() noexcept = 0;
    [[nodiscard]] virtual bool quitRequested() const noexcept = 0;
    [[nodiscard]] virtual std::string_view lastHostEventSummary() const noexcept = 0;
    [[nodiscard]] virtual std::string_view lastBackendError() const noexcept = 0;
    [[nodiscard]] virtual std::string backendStatusSummary() const = 0;
    [[nodiscard]] virtual bool handleHostEvent(const SdlFrontendHostEvent& event) = 0;
    [[nodiscard]] virtual std::string_view backendName() const noexcept = 0;
    [[nodiscard]] virtual bool backendReady() const noexcept = 0;
    [[nodiscard]] virtual bool audioOutputReady() const noexcept = 0;
    [[nodiscard]] virtual std::size_t bufferedAudioSamples() const noexcept = 0;
    [[nodiscard]] virtual uint32_t queuedAudioBytes() const noexcept = 0;
    [[nodiscard]] virtual bool tryInitializeBackend() = 0;
    [[nodiscard]] virtual std::size_t pumpBackendEvents() = 0;
};

struct SdlFrontendPluginApiV1 {
    std::size_t structSize = sizeof(SdlFrontendPluginApiV1);
    std::uint32_t apiVersion = kSdlFrontendPluginApiVersion;
    ISdlFrontendPlugin* (*create)(const SdlFrontendConfig*) = nullptr;
    void (*destroy)(ISdlFrontendPlugin*) noexcept = nullptr;
};

using GetSdlFrontendPluginApiV1Fn = const SdlFrontendPluginApiV1* (*)();

} // namespace BMMQ

#endif // BMMQ_SDL_FRONTEND_PLUGIN_HPP
