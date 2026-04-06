#ifndef BMMQ_PLUGIN_MANAGER_HPP
#define BMMQ_PLUGIN_MANAGER_HPP

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "IoPlugin.hpp"

namespace BMMQ {

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
        plugins_.push_back(Entry{std::move(plugin), false});
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

    void initialize(const MachineView& view)
    {
        for (auto& entry : plugins_) {
            if (entry.disabled || entry.attached) {
                continue;
            }
            try {
                entry.plugin->onAttach(view);
                entry.attached = true;
            } catch (...) {
                entry.disabled = true;
                entry.attached = false;
                entry.failure = std::current_exception();
            }
        }
        initialized_ = true;
    }

    void shutdown(const MachineView& view)
    {
        for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
            if (!it->attached) {
                continue;
            }
            try {
                it->plugin->onDetach(view);
            } catch (...) {
                it->disabled = true;
                it->failure = std::current_exception();
            }
            it->attached = false;
        }
        initialized_ = false;
    }

    void emit(const MachineView& view, const MachineEvent& event)
    {
        initialize(view);
        for (auto& entry : plugins_) {
            if (entry.disabled) {
                continue;
            }
            try {
                entry.plugin->onMachineEvent(event, view);
                dispatchTyped(*entry.plugin, event, view);
            } catch (...) {
                entry.disabled = true;
                entry.failure = std::current_exception();
            }
        }
    }

    std::optional<uint32_t> sampleDigitalInput(const MachineView& view)
    {
        initialize(view);
        for (auto& entry : plugins_) {
            if (entry.disabled) {
                continue;
            }
            auto* plugin = dynamic_cast<IDigitalInputPlugin*>(entry.plugin.get());
            if (plugin == nullptr) {
                continue;
            }
            try {
                if (auto sample = plugin->sampleDigitalInput(view); sample.has_value()) {
                    return sample;
                }
            } catch (...) {
                entry.disabled = true;
                entry.failure = std::current_exception();
            }
        }
        return std::nullopt;
    }

    std::optional<IAnalogInputPlugin::AnalogState> sampleAnalogInput(const MachineView& view)
    {
        initialize(view);
        for (auto& entry : plugins_) {
            if (entry.disabled) {
                continue;
            }
            auto* plugin = dynamic_cast<IAnalogInputPlugin*>(entry.plugin.get());
            if (plugin == nullptr) {
                continue;
            }
            try {
                if (auto sample = plugin->sampleAnalogInput(view); sample.has_value()) {
                    return sample;
                }
            } catch (...) {
                entry.disabled = true;
                entry.failure = std::current_exception();
            }
        }
        return std::nullopt;
    }

private:
    struct Entry {
        std::unique_ptr<IPlugin> plugin;
        bool attached = false;
        bool disabled = false;
        std::exception_ptr failure{};
    };

    static void dispatchTyped(IPlugin& plugin, const MachineEvent& event, const MachineView& view)
    {
        switch (event.category) {
        case PluginCategory::System:
            break;
        case PluginCategory::Video:
            if (auto* typed = dynamic_cast<IVideoPlugin*>(&plugin)) {
                typed->onVideoEvent(event, view);
            }
            break;
        case PluginCategory::Audio:
            if (auto* typed = dynamic_cast<IAudioPlugin*>(&plugin)) {
                typed->onAudioEvent(event, view);
            }
            break;
        case PluginCategory::DigitalInput:
            if (auto* typed = dynamic_cast<IDigitalInputPlugin*>(&plugin)) {
                typed->onDigitalInputEvent(event, view);
            }
            break;
        case PluginCategory::AnalogInput:
            if (auto* typed = dynamic_cast<IAnalogInputPlugin*>(&plugin)) {
                typed->onAnalogInputEvent(event, view);
            }
            break;
        case PluginCategory::Network:
            if (auto* typed = dynamic_cast<INetworkPlugin*>(&plugin)) {
                typed->onNetworkEvent(event, view);
            }
            break;
        case PluginCategory::Serial:
            if (auto* typed = dynamic_cast<ISerialPlugin*>(&plugin)) {
                typed->onSerialEvent(event, view);
            }
            break;
        case PluginCategory::Parallel:
            if (auto* typed = dynamic_cast<IParallelPlugin*>(&plugin)) {
                typed->onParallelEvent(event, view);
            }
            break;
        }
    }

    std::vector<Entry> plugins_;
    bool initialized_ = false;
};

} // namespace BMMQ

#endif // BMMQ_PLUGIN_MANAGER_HPP
