#ifndef BMMQ_PLUGIN_MANAGER_HPP
#define BMMQ_PLUGIN_MANAGER_HPP

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "IoPlugin.hpp"

namespace BMMQ {

struct PluginStatus {
    std::string id;
    std::string displayName;
    bool attached = false;
    bool disabled = false;
    std::size_t failureCount = 0;
    std::string lastError;
};

class PluginManager {
public:
    template<typename PluginT>
    PluginT& add(std::unique_ptr<PluginT> plugin)
    {
        static_assert(std::is_base_of_v<IPlugin, PluginT>, "PluginT must derive from BMMQ::IPlugin");
        if (!plugin) {
            throw std::invalid_argument("plugin must not be null");
        }
        PluginT& ref = *plugin;
        Entry entry;
        entry.plugin = std::move(plugin);
        cacheInterfaces(entry);
        plugins_.push_back(std::move(entry));
        return ref;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return plugins_.size();
    }

    [[nodiscard]] std::size_t disabledCount() const noexcept
    {
        std::size_t count = 0;
        for (const auto& entry : plugins_) {
            if (entry.disabled) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return initialized_;
    }

    [[nodiscard]] bool hasDisabledPlugins() const noexcept
    {
        return disabledCount() != 0;
    }

    [[nodiscard]] std::vector<PluginStatus> statuses() const
    {
        std::vector<PluginStatus> result;
        result.reserve(plugins_.size());
        for (const auto& entry : plugins_) {
            result.push_back(buildStatus(entry));
        }
        return result;
    }

    [[nodiscard]] std::optional<PluginStatus> statusFor(std::string_view id) const
    {
        for (const auto& entry : plugins_) {
            if (entry.plugin != nullptr && entry.plugin->id() == id) {
                return buildStatus(entry);
            }
        }
        return std::nullopt;
    }

    bool reenable(std::string_view id)
    {
        for (auto& entry : plugins_) {
            if (entry.plugin == nullptr || entry.plugin->id() != id) {
                continue;
            }
            entry.disabled = false;
            entry.lastError.clear();
            entry.failure = nullptr;
            return true;
        }
        return false;
    }

    void resetFailures()
    {
        for (auto& entry : plugins_) {
            entry.disabled = false;
            entry.failureCount = 0;
            entry.lastError.clear();
            entry.failure = nullptr;
        }
    }

    void initialize(MutableMachineView view)
    {
        for (auto& entry : plugins_) {
            if (entry.disabled || entry.attached) {
                continue;
            }
            try {
                entry.plugin->onAttach(view);
                entry.attached = true;
            } catch (...) {
                entry.attached = false;
                markFailure(entry);
            }
        }
        initialized_ = true;
    }

    void shutdown(MutableMachineView view)
    {
        for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
            if (!it->attached) {
                continue;
            }
            try {
                it->plugin->onDetach(view);
            } catch (...) {
                markFailure(*it);
            }
            it->attached = false;
        }
        initialized_ = false;
    }

    void emit(const MachineView& view, const MachineEvent& event)
    {
        for (auto& entry : plugins_) {
            if (entry.disabled) {
                continue;
            }
            try {
                entry.plugin->onMachineEvent(event, view);
                dispatchTyped(entry, event, view);
            } catch (...) {
                markFailure(entry);
            }
        }
    }

    void emit(MutableMachineView view, const MachineEvent& event)
    {
        initialize(view);
        emit(static_cast<const MachineView&>(view), event);
    }

    std::optional<uint32_t> sampleDigitalInput(const MachineView& view)
    {
        for (auto& entry : plugins_) {
            if (entry.disabled) {
                continue;
            }
            auto* plugin = entry.digitalInput;
            if (plugin == nullptr) {
                continue;
            }
            try {
                if (auto sample = plugin->sampleDigitalInput(view); sample.has_value()) {
                    return sample;
                }
            } catch (...) {
                markFailure(entry);
            }
        }
        return std::nullopt;
    }

    std::optional<uint32_t> sampleDigitalInput(MutableMachineView view)
    {
        initialize(view);
        return sampleDigitalInput(static_cast<const MachineView&>(view));
    }

    std::optional<IAnalogInputPlugin::AnalogState> sampleAnalogInput(const MachineView& view)
    {
        for (auto& entry : plugins_) {
            if (entry.disabled) {
                continue;
            }
            auto* plugin = entry.analogInput;
            if (plugin == nullptr) {
                continue;
            }
            try {
                if (auto sample = plugin->sampleAnalogInput(view); sample.has_value()) {
                    return sample;
                }
            } catch (...) {
                markFailure(entry);
            }
        }
        return std::nullopt;
    }

    std::optional<IAnalogInputPlugin::AnalogState> sampleAnalogInput(MutableMachineView view)
    {
        initialize(view);
        return sampleAnalogInput(static_cast<const MachineView&>(view));
    }

private:
    struct Entry {
        std::unique_ptr<IPlugin> plugin;
        IVideoPlugin* video = nullptr;
        IAudioPlugin* audio = nullptr;
        IDigitalInputPlugin* digitalInput = nullptr;
        IAnalogInputPlugin* analogInput = nullptr;
        INetworkPlugin* network = nullptr;
        ISerialPlugin* serial = nullptr;
        IParallelPlugin* parallel = nullptr;
        bool attached = false;
        bool disabled = false;
        std::size_t failureCount = 0;
        std::string lastError;
        std::exception_ptr failure{};
    };

    static PluginStatus buildStatus(const Entry& entry)
    {
        PluginStatus status;
        if (entry.plugin != nullptr) {
            status.id = std::string(entry.plugin->id());
            status.displayName = std::string(entry.plugin->displayName());
        }
        status.attached = entry.attached;
        status.disabled = entry.disabled;
        status.failureCount = entry.failureCount;
        status.lastError = entry.lastError;
        return status;
    }

    static void cacheInterfaces(Entry& entry) noexcept
    {
        entry.video = dynamic_cast<IVideoPlugin*>(entry.plugin.get());
        entry.audio = dynamic_cast<IAudioPlugin*>(entry.plugin.get());
        entry.digitalInput = dynamic_cast<IDigitalInputPlugin*>(entry.plugin.get());
        entry.analogInput = dynamic_cast<IAnalogInputPlugin*>(entry.plugin.get());
        entry.network = dynamic_cast<INetworkPlugin*>(entry.plugin.get());
        entry.serial = dynamic_cast<ISerialPlugin*>(entry.plugin.get());
        entry.parallel = dynamic_cast<IParallelPlugin*>(entry.plugin.get());
    }

    static void markFailure(Entry& entry)
    {
        entry.disabled = true;
        entry.failure = std::current_exception();
        ++entry.failureCount;
        try {
            if (entry.failure != nullptr) {
                std::rethrow_exception(entry.failure);
            }
        } catch (const std::exception& ex) {
            entry.lastError = ex.what();
        } catch (...) {
            entry.lastError = "unknown plugin failure";
        }
    }

    static void dispatchTyped(Entry& entry, const MachineEvent& event, const MachineView& view)
    {
        switch (event.category) {
        case PluginCategory::System:
            break;
        case PluginCategory::Video:
            if (entry.video != nullptr) {
                entry.video->onVideoEvent(event, view);
            }
            break;
        case PluginCategory::Audio:
            if (entry.audio != nullptr) {
                entry.audio->onAudioEvent(event, view);
            }
            break;
        case PluginCategory::DigitalInput:
            if (entry.digitalInput != nullptr) {
                entry.digitalInput->onDigitalInputEvent(event, view);
            }
            break;
        case PluginCategory::AnalogInput:
            if (entry.analogInput != nullptr) {
                entry.analogInput->onAnalogInputEvent(event, view);
            }
            break;
        case PluginCategory::Network:
            if (entry.network != nullptr) {
                entry.network->onNetworkEvent(event, view);
            }
            break;
        case PluginCategory::Serial:
            if (entry.serial != nullptr) {
                entry.serial->onSerialEvent(event, view);
            }
            break;
        case PluginCategory::Parallel:
            if (entry.parallel != nullptr) {
                entry.parallel->onParallelEvent(event, view);
            }
            break;
        }
    }

    std::vector<Entry> plugins_;
    bool initialized_ = false;
};

} // namespace BMMQ

#endif // BMMQ_PLUGIN_MANAGER_HPP
