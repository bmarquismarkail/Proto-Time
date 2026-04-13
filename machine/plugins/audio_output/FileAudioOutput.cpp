#include "FileAudioOutput.hpp"

#include "../../AudioService.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace BMMQ {

class FileAudioOutputBackend::Impl {
public:
    enum class RuntimeError : std::uint8_t {
        None = 0,
        WriteFailed,
    };

    ~Impl()
    {
        close();
    }

    [[nodiscard]] std::string_view name() const noexcept
    {
        return "File audio output";
    }

    [[nodiscard]] bool open(AudioEngine& engine, const AudioOutputOpenConfig& config)
    {
        close();
        lastError_.clear();
        runtimeError_.store(RuntimeError::None, std::memory_order_release);
        lastErrorCode_.store(AudioOutputErrorCode::None, std::memory_order_release);

        if (config.audioService == nullptr) {
            lastError_ = "Audio service is required";
            lastErrorCode_.store(AudioOutputErrorCode::InvalidConfig, std::memory_order_release);
            return false;
        }
        if (config.filePath.empty()) {
            lastError_ = "File path is required";
            lastErrorCode_.store(AudioOutputErrorCode::InvalidPath, std::memory_order_release);
            return false;
        }
        if (config.channels != 1) {
            lastError_ = "Only mono output is supported";
            lastErrorCode_.store(AudioOutputErrorCode::UnsupportedConfig, std::memory_order_release);
            return false;
        }

        errno = 0;
        const auto openMode = config.appendToFile
            ? (std::ios::binary | std::ios::out | std::ios::app)
            : (std::ios::binary | std::ios::out | std::ios::trunc);
        output_.open(config.filePath, openMode);
        if (!output_.is_open()) {
            if (errno == EACCES || errno == EPERM) {
                lastErrorCode_.store(AudioOutputErrorCode::PermissionDenied, std::memory_order_release);
            } else {
                lastErrorCode_.store(AudioOutputErrorCode::InvalidPath, std::memory_order_release);
            }
            lastError_ = "Failed to open output file";
            return false;
        }

        service_ = config.audioService;
        engine_ = &engine;
        service_->setBackendPausedOrClosed(true);
        deviceInfo_.sampleRate = config.testForcedDeviceSampleRate > 0
            ? config.testForcedDeviceSampleRate
            : std::max(config.requestedSampleRate, 1);
        deviceInfo_.callbackChunkSamples = std::max<std::size_t>(config.callbackChunkSamples, 1u);
        deviceInfo_.channels = 1;
        engine_->setDeviceSampleRate(deviceInfo_.sampleRate);
        if (!service_->configureFixedCallbackCapacity(deviceInfo_.callbackChunkSamples)) {
            lastError_ = "Failed to configure callback buffer capacity";
            lastErrorCode_.store(AudioOutputErrorCode::InvalidConfig, std::memory_order_release);
            engine_ = nullptr;
            service_ = nullptr;
            output_.close();
            return false;
        }

        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this]() { run(); });
        ready_.store(true, std::memory_order_release);
        service_->setBackendPausedOrClosed(false);
        return true;
    }

    void close() noexcept
    {
        if (service_ != nullptr) {
            service_->setBackendPausedOrClosed(true);
        }
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        ready_.store(false, std::memory_order_release);
        if (output_.is_open()) {
            output_.flush();
            output_.close();
        }
        engine_ = nullptr;
        service_ = nullptr;
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return ready_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string_view lastError() const noexcept
    {
        switch (runtimeError_.load(std::memory_order_acquire)) {
        case RuntimeError::WriteFailed:
            return "File write failed";
        case RuntimeError::None:
            break;
        }
        return lastError_;
    }

    [[nodiscard]] AudioOutputErrorCode lastErrorCode() const noexcept
    {
        return lastErrorCode_.load(std::memory_order_acquire);
    }

    [[nodiscard]] AudioOutputDeviceInfo deviceInfo() const noexcept
    {
        return deviceInfo_;
    }

private:
    void run()
    {
        if (service_ == nullptr || !output_.is_open()) {
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
                service_->renderForOutput(buffer);
                output_.write(reinterpret_cast<const char*>(buffer.data()),
                              static_cast<std::streamsize>(buffer.size() * sizeof(int16_t)));
                if (!output_) {
                    if (errno == ENOSPC) {
                        lastErrorCode_.store(AudioOutputErrorCode::DiskFull, std::memory_order_release);
                    } else if (errno == EACCES || errno == EPERM) {
                        lastErrorCode_.store(AudioOutputErrorCode::PermissionDenied, std::memory_order_release);
                    } else {
                        lastErrorCode_.store(AudioOutputErrorCode::WriteFailed, std::memory_order_release);
                    }
                    runtimeError_.store(RuntimeError::WriteFailed, std::memory_order_release);
                    ready_.store(false, std::memory_order_release);
                    running_.store(false, std::memory_order_release);
                    break;
                }
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
    AudioService* service_ = nullptr;
    AudioOutputDeviceInfo deviceInfo_{};
    std::string lastError_;
    std::atomic<AudioOutputErrorCode> lastErrorCode_{AudioOutputErrorCode::None};
    std::atomic<RuntimeError> runtimeError_{RuntimeError::None};
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::thread worker_{};
    std::ofstream output_{};
};

FileAudioOutputBackend::FileAudioOutputBackend() = default;
FileAudioOutputBackend::~FileAudioOutputBackend() = default;

std::string_view FileAudioOutputBackend::name() const noexcept
{
    return impl_ != nullptr ? impl_->name() : "File audio output";
}

bool FileAudioOutputBackend::open(AudioEngine& engine, const AudioOutputOpenConfig& config)
{
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    return impl_->open(engine, config);
}

void FileAudioOutputBackend::close() noexcept
{
    if (impl_ != nullptr) {
        impl_->close();
    }
}

bool FileAudioOutputBackend::ready() const noexcept
{
    return impl_ != nullptr && impl_->ready();
}

std::string_view FileAudioOutputBackend::lastError() const noexcept
{
    return impl_ != nullptr ? impl_->lastError() : std::string_view{};
}

AudioOutputErrorCode FileAudioOutputBackend::lastErrorCode() const noexcept
{
    return impl_ != nullptr ? impl_->lastErrorCode() : AudioOutputErrorCode::None;
}

AudioOutputDeviceInfo FileAudioOutputBackend::deviceInfo() const noexcept
{
    return impl_ != nullptr ? impl_->deviceInfo() : AudioOutputDeviceInfo{};
}

} // namespace BMMQ
