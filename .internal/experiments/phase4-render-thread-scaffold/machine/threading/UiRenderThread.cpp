#include "machine/threading/UiRenderThread.hpp"

#include <thread>
#include <chrono>
#include <cstddef>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <queue>
#include <vector>
#include "machine/VideoService.hpp"
#include "machine/Machine.hpp"
#include "machine/threading/UiRenderThread.hpp"
#include "machine/InputService.hpp"
#include "machine/AudioService.hpp"

namespace BMMQ {

// ============================================================================
// Implementation of UiRenderThread
// ============================================================================

UiRenderThread::UiRenderThread(
    std::shared_ptr<Machine> machine,
    std::shared_ptr<InputService> inputService,
    std::shared_ptr<AudioService> audioService)
    : machine_(std::move(machine))
    , inputService_(std::move(inputService))
    , audioService_(std::move(audioService))
    , videoService_(std::make_shared<VideoService>(
          machine,
          std::unique_ptr<AudioService>(std::move(audioService_)),
          std::unique_ptr<InputService>(std::move(inputService_)),
          std::make_unique<VisualOverrideService>(),
          std::make_unique<VisualDebugAdapter>()))
{
}

bool UiRenderThread::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

void UiRenderThread::requestShutdown() noexcept
{
    running_.store(false, std::memory_order_release);
}

void UiRenderThread::signalFrameReady(std::shared_ptr<VideoFramePacket> packet)
{
    std::lock_guard<std::mutex> lock(eventMutex_);
    eventQueue_.emplace_back(std::move(packet));
    eventCv_.notify_one();
}

void UiRenderThread::processEvents()
{
    while (running_.load(std::memory_order_acquire)) {
        // Poll SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            // Handle input events
            if (event.type == SDL_EVENT_KEYDOWN || event.type == SDL_EVENT_KEYUP) {
                inputService_->processInput(event);
            }
            else if (event.type == SDL_EVENT_QUIT) {
                running_.store(false, std::memory_order_release);
            }
        }
    }
}

void UiRenderThread::presentFrame(std::shared_ptr<VideoFramePacket> packet)
{
    // TODO: Present the frame to the SDL window using SDL_RenderPresent()
    // This is a placeholder - the actual SDL rendering logic should be implemented
    std::cout << "Presenting frame: " << packet->width << "x" << packet->height << " at generation " << packet->generation << std::endl;
}

void UiRenderThread::loop()
{
    while (running_.load(std::memory_order_acquire)) {
        // Process incoming events
        std::shared_ptr<VideoFramePacket> frame;
        {
            std::lock_guard<std::mutex> lock(eventMutex_);
            if (!eventQueue_.empty()) {
                frame = std::move(eventQueue_[nextEventIndex_]);
                nextEventIndex_++;
            }
        }

        if (frame) {
            // Signal frame presentation to the main thread
            videoService_->signalFramePresent(frame);
        }

        // Process SDL events
        processEvents();
    }
}

void UiRenderThread::run() noexcept
{
    // Initialize SDL
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Create SDL window
    SDL_Window* window = SDL_CreateWindow("Proto-Time Emulator",
                                            SDL_WINDOWPOS_CENTERED,
                                            SDL_WINDOWPOS_CENTERED,
                                            800,
                                            600,
                                            SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        return;
    }

    // Create OpenGL context
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        return;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        return;
    }

    // Create texture for frame presentation
    const auto width = static_cast<int>(800);
    const auto height = static_cast<int>(600);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        return;
    }

    // Store SDL context
    window_ = window;
    glContext_ = glContext;
    renderer_ = renderer;
    texture_ = texture;
    eventQueue_ = new SDL_Event();

    // Start the render thread
    std::thread renderThread(this, &UiRenderThread::loop);

    // Keep the main thread alive
    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup
    renderThread.join();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    delete[] eventQueue_;
}

} // namespace BMMQ
