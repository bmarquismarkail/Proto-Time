constexpr struct { int width; int height; } kGameBoyResolution{160, 144};
#ifndef BMMQ_LOGGING_PLUGINS_HPP
#define BMMQ_LOGGING_PLUGINS_HPP

#include <cstddef>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IoPlugin.hpp"

namespace BMMQ {

namespace detail {

inline const char* machineEventTypeName(MachineEventType type)
{
    switch (type) {
    case MachineEventType::Attached:
        return "Attached";
    case MachineEventType::Detached:
        return "Detached";
    case MachineEventType::StepCompleted:
        return "StepCompleted";
    case MachineEventType::VBlank:
        return "VBlank";
    case MachineEventType::AudioFrameReady:
        return "AudioFrameReady";
    case MachineEventType::DigitalInputChanged:
        return "DigitalInputChanged";
    case MachineEventType::AnalogInputChanged:
        return "AnalogInputChanged";
    case MachineEventType::NetworkActivity:
        return "NetworkActivity";
    case MachineEventType::SerialActivity:
        return "SerialActivity";
    case MachineEventType::ParallelActivity:
        return "ParallelActivity";
    case MachineEventType::MemoryWriteObserved:
        return "MemoryWriteObserved";
    case MachineEventType::BootRomVisibilityChanged:
        return "BootRomVisibilityChanged";
    case MachineEventType::RomLoaded:
        return "RomLoaded";
    case MachineEventType::VideoScanlineReady:
        return "VideoScanlineReady";
    case MachineEventType::VisualResourceObserved:
        return "VisualResourceObserved";
    case MachineEventType::VisualResourceDecoded:
        return "VisualResourceDecoded";
    case MachineEventType::VisualOverrideResolved:
        return "VisualOverrideResolved";
    case MachineEventType::VisualPackMiss:
        return "VisualPackMiss";
    case MachineEventType::FrameCompositionStarted:
        return "FrameCompositionStarted";
    case MachineEventType::FrameCompositionCompleted:
        return "FrameCompositionCompleted";
    }
    return "Unknown";
}

inline std::string hexByte(uint8_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(value);
    return stream.str();
}

inline std::string hexWord(uint16_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
           << static_cast<unsigned>(value);
    return stream.str();
}

} // namespace detail

class LoggingPluginSupport {
public:
    using LogSink = std::function<void(std::string_view)>;

    [[nodiscard]] std::size_t entryCount() const noexcept
    {
        return entries_.size();
    }

    [[nodiscard]] const std::vector<std::string>& entries() const noexcept
    {
        return entries_;
    }

    [[nodiscard]] std::string_view lastEntry() const noexcept
    {
        return entries_.empty() ? std::string_view{} : std::string_view(entries_.back());
    }

    void setSink(LogSink sink)
    {
        sink_ = std::move(sink);
    }

    void clearSink()
    {
        sink_ = {};
    }

    void setLogFile(std::string path)
    {
        logFilePath_ = std::move(path);
    }

    void clearLogFile()
    {
        logFilePath_.clear();
    }

    [[nodiscard]] std::string_view logFilePath() const noexcept
    {
        return logFilePath_;
    }

protected:
    void setMaxEntryCount(std::size_t maxEntryCount) noexcept
    {
        maxEntryCount_ = maxEntryCount;
        trimEntries();
    }

    void appendLog(std::string entry)
    {
        entries_.push_back(entry);
        trimEntries();
        if (sink_) {
            sink_(entry);
        }
        if (!logFilePath_.empty()) {
            std::ofstream file(logFilePath_, std::ios::app);
            file << entry << '\n';
        }
    }

private:
    void trimEntries()
    {
        if (maxEntryCount_ == 0u || entries_.size() <= maxEntryCount_) {
            return;
        }
        const auto overflow = entries_.size() - maxEntryCount_;
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(overflow));
    }

    std::vector<std::string> entries_;
    LogSink sink_;
    std::string logFilePath_;
    std::size_t maxEntryCount_ = 0;
};

class LoggingVideoPlugin final : public IVideoPlugin, public LoggingPluginSupport {
public:
    std::string_view id() const override
    {
        return "bmmq.logging.video";
    }

    std::string_view displayName() const override
    {
        return "Logging Video Plugin";
    }

    void onAttach(MutableMachineView&) override
    {
        appendLog("video: attached");
    }

    void onDetach(MutableMachineView&) override
    {
        appendLog("video: detached");
    }

    void onVideoEvent(const MachineEvent& event, const MachineView& view) override
    {
        std::ostringstream stream;
        stream << "video: event=" << detail::machineEventTypeName(event.type)
               << " tick=" << event.tick
               << " addr=" << detail::hexWord(event.address);
        if (const auto model = view.videoDebugFrameModel({kGameBoyResolution.width, kGameBoyResolution.height}); model.has_value()) {
            stream << " display=" << (model->displayEnabled ? "on" : "off");
            if (model->scanlineIndex.has_value()) {
                stream << " scanline=" << static_cast<unsigned>(*model->scanlineIndex);
            }
        }
        appendLog(stream.str());
    }
};

class LoggingAudioPlugin final : public IAudioPlugin, public LoggingPluginSupport {
public:
    std::string_view id() const override
    {
        return "bmmq.logging.audio";
    }

    std::string_view displayName() const override
    {
        return "Logging Audio Plugin";
    }

    void onAttach(MutableMachineView&) override
    {
        appendLog("audio: attached");
    }

    void onDetach(MutableMachineView&) override
    {
        appendLog("audio: detached");
    }

    void onAudioEvent(const MachineEvent& event, const MachineView& view) override
    {
        std::ostringstream stream;
        stream << "audio: event=" << detail::machineEventTypeName(event.type)
               << " tick=" << event.tick
               << " addr=" << detail::hexWord(event.address);
        if (const auto state = view.audioState(); state.has_value()) {
            stream << " nr12=" << detail::hexByte(state->nr12)
                   << " nr52=" << detail::hexByte(state->nr52);
        }
        appendLog(stream.str());
    }
};

class LoggingDigitalInputPlugin final : public IDigitalInputPlugin, public LoggingPluginSupport {
public:
    std::string_view id() const override
    {
        return "bmmq.logging.digital-input";
    }

    std::string_view displayName() const override
    {
        return "Logging Digital Input Plugin";
    }

    void onAttach(MutableMachineView&) override
    {
        appendLog("input: attached");
    }

    void onDetach(MutableMachineView&) override
    {
        appendLog("input: detached");
    }

    void onDigitalInputEvent(const MachineEvent& event, const MachineView& view) override
    {
        std::ostringstream stream;
        stream << "input: event=" << detail::machineEventTypeName(event.type)
               << " tick=" << event.tick
               << " value=" << detail::hexByte(event.value);
        if (const auto state = view.digitalInputState(); state.has_value()) {
            stream << " joyp=" << detail::hexByte(state->joypRegister)
                   << " pressed=" << detail::hexByte(state->pressedMask);
        }
        appendLog(stream.str());
    }
};

} // namespace BMMQ

#endif // BMMQ_LOGGING_PLUGINS_HPP
