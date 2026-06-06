#include "DummyAudioOutput.hpp"

#include "../../AudioService.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace BMMQ {

class DummyAudioOutputBackend::Impl {
public:
    ~Impl()
    {
        close();
    }

    [[nodiscard]] std::string_view name() const noexcept
    {
        return "Dummy audio output";
    }

    [[nodiscard]] bool open(AudioEngine& engine, const AudioOutputOpenConfig& config)
    {
        close();
        clearError();

        if (config.audioService == nullptr) {
            setError(AudioOutputErrorCode::InvalidConfig, "Audio service is required");
            return false;
        }
        if (config.channels < 1 || config.channels > 2) {
            setError(AudioOutputErrorCode::UnsupportedConfig, "Only mono or stereo output is supported");
            return false;
        }
        service_ = config.audioService;
        engine_ = &engine;
        service_->setBackendPausedOrClosed(true);
        AudioOutputDeviceInfo deviceInfo{};
        deviceInfo.sampleRate = config.testForcedDeviceSampleRate > 0
            ? config.testForcedDeviceSampleRate
            : std::max(config.requestedSampleRate, 1);
        deviceInfo.callbackChunkSamples = std::max<std::size_t>(config.callbackChunkSamples, 1u);
        deviceInfo.channels = config.channels;
        {
            std::lock_guard<std::mutex> lock(deviceInfoMutex_);
            deviceInfo_ = deviceInfo;
        }
        engine_->setDeviceSampleRate(deviceInfo.sampleRate);
        if (!service_->configureFixedCallbackCapacity(deviceInfo.callbackChunkSamples)) {
            setError(AudioOutputErrorCode::InvalidConfig, "Failed to configure callback buffer capacity");
            engine_ = nullptr;
            service_ = nullptr;
            return false;
        }
        if (!service_->configureOutputTransport({
                .deviceSampleRate = deviceInfo.sampleRate,
                .channelCount = static_cast<uint8_t>(deviceInfo.channels),
                .callbackChunkSamples = deviceInfo.callbackChunkSamples,
                .readyQueueChunks = std::clamp<std::size_t>(config.readyQueueChunks, 1u, 64u),
            })) {
            setError(AudioOutputErrorCode::InvalidConfig, "Failed to configure output transport");
            engine_ = nullptr;
            service_ = nullptr;
            return false;
        }
        if (!service_->startOutputTransport()) {
            setError(AudioOutputErrorCode::RuntimeError, "Failed to start output transport");
            engine_ = nullptr;
            service_ = nullptr;
            return false;
        }

        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this]() { run(); });
        ready_.store(true, std::memory_order_release);
        return true;
    }

    void close() noexcept
    {
        ready_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (service_ != nullptr) {
            service_->stopOutputTransport();
            service_->setBackendPausedOrClosed(true);
        }
        engine_ = nullptr;
        service_ = nullptr;
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return ready_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string lastError() const noexcept
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastError_;
    }

    [[nodiscard]] AudioOutputErrorCode lastErrorCode() const noexcept
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        return lastErrorCode_;
    }

    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept
    {
        std::lock_guard<std::mutex> lock(deviceInfoMutex_);
        return deviceInfo_;
    }

private:
    void clearError() noexcept
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_.clear();
        lastErrorCode_ = AudioOutputErrorCode::None;
    }

    void setError(AudioOutputErrorCode code, std::string_view message)
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = std::string(message);
        lastErrorCode_ = code;
    }

    void run()
    {
        if (engine_ == nullptr || service_ == nullptr) {
            return;
        }
        std::vector<int16_t> buffer(deviceInfo_.callbackChunkSamples, 0);
        auto sleepDuration = std::chrono::milliseconds(5);
        if (deviceInfo_.sampleRate > 0) {
            const auto channels = std::max(deviceInfo_.channels, 1);
            const auto durationMs = static_cast<long long>(
                ((static_cast<double>(deviceInfo_.callbackChunkSamples) / static_cast<double>(channels)) /
                 deviceInfo_.sampleRate) * 1000.0
            );
            sleepDuration = std::chrono::milliseconds(std::max(1LL, durationMs));
        }
        while (running_.load(std::memory_order_acquire)) {
            try {
                service_->drainReadyOutput(buffer);
                std::this_thread::sleep_for(sleepDuration);
            } catch (const std::exception& ex) {
                setError(AudioOutputErrorCode::RuntimeError, ex.what());
                ready_.store(false, std::memory_order_release);
                running_.store(false, std::memory_order_release);
                break;
            } catch (...) {
                setError(AudioOutputErrorCode::RuntimeError, "Unknown audio output runtime error");
                ready_.store(false, std::memory_order_release);
                running_.store(false, std::memory_order_release);
                break;
            }
        }
    }

    AudioEngine* engine_ = nullptr;
    AudioService* service_ = nullptr;
    AudioOutputDeviceInfo deviceInfo_{};
    mutable std::mutex deviceInfoMutex_;
    mutable std::mutex errorMutex_;
    std::string lastError_;
    AudioOutputErrorCode lastErrorCode_{AudioOutputErrorCode::None};
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::thread worker_{};
};

DummyAudioOutputBackend::DummyAudioOutputBackend() = default;
DummyAudioOutputBackend::~DummyAudioOutputBackend() = default;

std::string_view DummyAudioOutputBackend::name() const noexcept
{
    return impl_ != nullptr ? impl_->name() : "Dummy audio output";
}

bool DummyAudioOutputBackend::open(AudioEngine& engine, const AudioOutputOpenConfig& config)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->open(engine, config);
}

void DummyAudioOutputBackend::close() noexcept
{
    if (impl_ != nullptr) {
        impl_->close();
    }
}

bool DummyAudioOutputBackend::ready() const noexcept
{
    return impl_ != nullptr && impl_->ready();
}

std::string DummyAudioOutputBackend::lastError() const noexcept
{
    return impl_ != nullptr ? impl_->lastError() : std::string{};
}

AudioOutputErrorCode DummyAudioOutputBackend::lastErrorCode() const noexcept
{
    return impl_ != nullptr ? impl_->lastErrorCode() : AudioOutputErrorCode::None;
}

AudioOutputDeviceInfo DummyAudioOutputBackend::deviceInfo() const noexcept
{
    return impl_ != nullptr ? impl_->deviceInfo() : AudioOutputDeviceInfo{};
}

} // namespace BMMQ
