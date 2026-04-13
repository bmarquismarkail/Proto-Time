#include "DummyAudioOutput.hpp"

#include "../AudioEngine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
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
        lastError_.clear();

        engine_ = &engine;
        deviceInfo_.sampleRate = config.testForcedDeviceSampleRate > 0
            ? config.testForcedDeviceSampleRate
            : std::max(config.requestedSampleRate, 1);
        deviceInfo_.callbackChunkSamples = std::max<std::size_t>(config.callbackChunkSamples, 1u);
        deviceInfo_.channels = 1;
        engine_->setDeviceSampleRate(deviceInfo_.sampleRate);

        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this]() { run(); });
        ready_.store(true, std::memory_order_release);
        return true;
    }

    void close() noexcept
    {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        ready_.store(false, std::memory_order_release);
        engine_ = nullptr;
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return ready_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string_view lastError() const noexcept
    {
        return lastError_;
    }

    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept
    {
        return deviceInfo_;
    }

private:
    void run()
    {
        if (engine_ == nullptr) {
            return;
        }
        std::vector<int16_t> buffer(deviceInfo_.callbackChunkSamples, 0);
        auto sleepDuration = std::chrono::milliseconds(5);
        if (deviceInfo_.sampleRate > 0) {
            const auto durationMs = static_cast<long long>(
                (static_cast<double>(deviceInfo_.callbackChunkSamples) / deviceInfo_.sampleRate) * 1000.0
            );
            sleepDuration = std::chrono::milliseconds(std::max(1LL, durationMs));
        }
        while (running_.load(std::memory_order_acquire)) {
            try {
                engine_->render(buffer);
                std::this_thread::sleep_for(sleepDuration);
            } catch (const std::exception&) {
                running_.store(false, std::memory_order_release);
                break;
            } catch (...) {
                running_.store(false, std::memory_order_release);
                break;
            }
        }
    }

    AudioEngine* engine_ = nullptr;
    AudioOutputDeviceInfo deviceInfo_{};
    std::string lastError_;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::thread worker_{};
};

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

std::string_view DummyAudioOutputBackend::lastError() const noexcept
{
    return impl_ != nullptr ? impl_->lastError() : std::string_view{};
}

AudioOutputDeviceInfo DummyAudioOutputBackend::deviceInfo() const noexcept
{
    return impl_ != nullptr ? impl_->deviceInfo() : AudioOutputDeviceInfo{};
}

} // namespace BMMQ
