#ifndef BMMQ_MACHINE_HPP
#define BMMQ_MACHINE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "AudioService.hpp"
#include "InputService.hpp"
#include "RegisterId.hpp"
#include "RuntimeContext.hpp"
#include "VideoService.hpp"
#include "TimingService.hpp"
#include "plugins/IoPlugin.hpp"
#include "plugins/PluginManager.hpp"

namespace BMMQ::Plugin {
class IExecutorPolicyPlugin;
}

namespace BMMQ {

class Machine {
public:
    virtual ~Machine() = default;
    Machine()
        : audioService_(std::make_unique<AudioService>()),
          inputService_(std::make_unique<InputService>()),
                    videoService_(std::make_unique<VideoService>()),
                    timingService_(std::make_unique<TimingService>()) {}
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;
    virtual void loadRom(const std::vector<uint8_t>& bytes) = 0;
    virtual RuntimeContext& runtimeContext() = 0;
    virtual const RuntimeContext& runtimeContext() const = 0;
    virtual PluginManager& pluginManager() = 0;
    virtual const PluginManager& pluginManager() const = 0;
    [[nodiscard]] AudioService& audioService() {
        return *audioService_;
    }
    [[nodiscard]] const AudioService& audioService() const {
        return *audioService_;
    }
    [[nodiscard]] VideoService& videoService() {
        return *videoService_;
    }
    [[nodiscard]] const VideoService& videoService() const {
        return *videoService_;
    }
    [[nodiscard]] InputService& inputService() {
        return *inputService_;
    }
    [[nodiscard]] const InputService& inputService() const {
        return *inputService_;
    }
    [[nodiscard]] bool setAudioService(std::unique_ptr<AudioService> service) {
        if (!service) {
            return false;
        }
        if (pluginManager().initialized()) {
            return false;
        }
        audioService_ = std::move(service);
        return true;
    }
    [[nodiscard]] bool setVideoService(std::unique_ptr<VideoService> service) {
        if (!service) {
            return false;
        }
        if (pluginManager().initialized()) {
            return false;
        }
        videoService_ = std::move(service);
        return true;
    }
    [[nodiscard]] bool setInputService(std::unique_ptr<InputService> service) {
        if (!service) {
            return false;
        }
        if (pluginManager().initialized()) {
            return false;
        }
        inputService_ = std::move(service);
        return true;
    }
    [[nodiscard]] TimingService& timingService() {
        return *timingService_;
    }

    [[nodiscard]] const TimingService& timingService() const {
        return *timingService_;
    }

    [[nodiscard]] bool setTimingService(std::unique_ptr<TimingService> service) {
        if (!service) {
            return false;
        }
        if (pluginManager().initialized()) {
            return false;
        }
        timingService_ = std::move(service);
        return true;
    }
    virtual std::span<const IoRegionDescriptor> describeIoRegions() const = 0;
    virtual void attachExecutorPolicy(Plugin::IExecutorPolicyPlugin& policy) = 0;
    virtual const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const = 0;
    [[nodiscard]] MachineView view() const {
        const auto regions = describeIoRegions();
        return MachineView{*this, runtimeContext(), std::vector<IoRegionDescriptor>(regions.begin(), regions.end())};
    }
    [[nodiscard]] MutableMachineView view() {
        const auto regions = describeIoRegions();
        return MutableMachineView{*this, runtimeContext(), std::vector<IoRegionDescriptor>(regions.begin(), regions.end())};
    }
    [[nodiscard]] MutableMachineView mutableView() {
        const auto regions = describeIoRegions();
        return MutableMachineView{*this, runtimeContext(), std::vector<IoRegionDescriptor>(regions.begin(), regions.end())};
    }
    virtual std::optional<uint32_t> currentDigitalInputMask() const {
        return std::nullopt;
    }
    virtual std::vector<int16_t> recentAudioSamples() const {
        return {};
    }
    virtual uint32_t audioSampleRate() const {
        return 48000u;
    }
    virtual uint64_t audioFrameCounter() const {
        return 0u;
    }
    virtual uint32_t clockHz() const {
        return runtimeContext().clockHz();
    }
    virtual ExecutionGuarantee guarantee() const noexcept {
        return runtimeContext().guarantee();
    }
    virtual void step() {
        runtimeContext().step();
    }
    virtual uint16_t readRegisterPair(std::string_view id) const = 0;

private:
    std::unique_ptr<AudioService> audioService_;
    std::unique_ptr<InputService> inputService_;
    std::unique_ptr<VideoService> videoService_;
    std::unique_ptr<TimingService> timingService_;
};

inline std::optional<uint32_t> queryDigitalInputMask(const Machine& machine) {
    return machine.currentDigitalInputMask();
}

inline AudioService& queryAudioService(Machine& machine) {
    return machine.audioService();
}

inline const AudioService& queryAudioService(const Machine& machine) {
    return machine.audioService();
}

inline InputService& queryInputService(Machine& machine) {
    return machine.inputService();
}

inline const InputService& queryInputService(const Machine& machine) {
    return machine.inputService();
}

inline VideoService& queryVideoService(Machine& machine) {
    return machine.videoService();
}

inline const VideoService& queryVideoService(const Machine& machine) {
    return machine.videoService();
}

inline TimingService& queryTimingService(Machine& machine) {
    return machine.timingService();
}

inline const TimingService& queryTimingService(const Machine& machine) {
    return machine.timingService();
}

inline std::vector<int16_t> queryRecentAudioSamples(const Machine& machine) {
    return machine.recentAudioSamples();
}

inline uint32_t queryAudioSampleRate(const Machine& machine) {
    return machine.audioSampleRate();
}

inline uint64_t queryAudioFrameCounter(const Machine& machine) {
    return machine.audioFrameCounter();
}

} // namespace BMMQ

#endif // BMMQ_MACHINE_HPP
