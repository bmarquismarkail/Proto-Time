#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace BMMQ {

struct RiverXmbContext {
    std::string emulator = "T.I.M.E.";
    std::string platform = "Game Boy";
    std::string game = "Game Boy";
    std::string title = "Game Boy";
    std::string presentation = "gameboy";
    std::string artworkKey = "gameboy";
    std::string visualizer = "off";
    std::string visualizerColor = "#d94b4b";
};

[[nodiscard]] RiverXmbContext makeGameBoyContext(std::string_view cartridgeTitle);

struct RiverXmbTelemetry {
    std::string location = "Pallet Town";
    unsigned playTime = 0;
    unsigned badges = 0;
    unsigned hp = 20;
    unsigned maxHp = 20;
};

class RiverXmbIntegration final {
public:
    explicit RiverXmbIntegration(std::size_t capacity = 8);
    ~RiverXmbIntegration();
    RiverXmbIntegration(const RiverXmbIntegration&) = delete;

    void contextSet(const RiverXmbContext& context);
    void contextUpdate(const RiverXmbContext& context);
    void telemetryUpdate(const RiverXmbTelemetry& telemetry);
    void contextClear();
    void stop();

    [[nodiscard]] static std::string contextJson(const RiverXmbContext& context, std::string_view action);
    [[nodiscard]] static std::string telemetryJson(const RiverXmbTelemetry& telemetry);
    [[nodiscard]] static std::optional<std::string> socketPath();

private:
    void enqueue(std::string message, bool telemetry);
    void worker();
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> queue_;
    std::size_t capacity_;
    bool stopping_ = false;
    std::optional<std::string> lastTelemetry_;
    std::thread thread_;
};

} // namespace BMMQ
