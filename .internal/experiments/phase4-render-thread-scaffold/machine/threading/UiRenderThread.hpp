#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace BMMQ {


namespace BMMQ {







class VideoService;

/**
 * @brief UI/Render Thread for Proto-Time emulator
 *
 * This class provides a dedicated thread for SDL event handling and video
 * presentation, separating these operations from the main emulation loop.
 * The emulation thread signals frames via VideoService::signalFramePresent(),
 * and the UI render thread handles SDL presentation.
 */
class UiRenderThread {
public:
    /**
     * Constructor
     * @param machine Shared pointer to the main Machine object
     * @param audioService Audio service for audio output
     * @param inputService Input service for input handling
     */
    explicit UiRenderThread(
        std::shared_ptr<Machine> machine,
        std::shared_ptr<InputService> inputService,
        std::shared_ptr<AudioService> audioService);

    /**
     * Run the UI render thread
     */
    void run() noexcept;

    /**
     * Signal that a frame is ready to be presented
     * @param packet Shared pointer to the video frame packet
     */
    void signalFrameReady(std::shared_ptr<VideoFramePacket> packet);

    /**
     * Check if the thread should continue running
     * @return true if the thread should continue
     */
    bool isRunning() const noexcept;

    /**
     * Request shutdown
     */
    void requestShutdown() noexcept;

private:
    /**
     * Process SDL events
     */
    void processEvents();

    /**
     * Present a frame to the SDL window
     * @param packet The video frame packet to present
     */
    void presentFrame(std::shared_ptr<VideoFramePacket> packet);

    /**
     * Main thread loop
     */
    void loop();

    // Machine shared pointer for service access
    std::shared_ptr<Machine> machine_;

    // Input and audio services
    std::shared_ptr<InputService> inputService_;
    std::shared_ptr<AudioService> audioService_;

    // VideoService for frame presentation signaling
    std::shared_ptr<VideoService> videoService_;

    // Shutdown flag
    std::atomic<bool> running_{true};

    // Event queue
    std::mutex eventMutex_;
    std::condition_variable eventCv_;
    std::vector<std::shared_ptr<VideoFramePacket>> eventQueue_;
    std::atomic<std::size_t> nextEventIndex_{0};

    // SDL window handle and context
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Event* eventQueue_ = nullptr;

    // Frame buffer
    SDL_Texture* texture_ = nullptr;

    // Frame timing
    std::chrono::steady_clock::time_point lastPresentedTime_;
    std::chrono::steady_clock::time_point lastEventTime_;
    std::chrono::steady_clock::time_point lastPresentEventTime_;
};

} // namespace BMMQ


} // namespace BMMQ
