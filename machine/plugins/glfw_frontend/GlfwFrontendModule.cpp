#include "machine/plugins/abi/TimePluginAbi.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
#  include <GLFW/glfw3.h>
#endif

namespace {

#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
std::size_t glfwUserCount = 0u;
#endif

class GlfwFrontend final {
public:
    GlfwFrontend(const TimeFrontendHostApiV1& host, const TimeFrontendConfigV1& config)
        : host_(host), title_(config.window_title != nullptr ? config.window_title : "Proto-Time"),
          scale_(std::max(config.window_scale, 1u)),
          frameWidth_(std::max(config.frame_width, 1)),
          frameHeight_(std::max(config.frame_height, 1)), flags_(config.flags),
          visibilityRequested_((config.flags & TIME_FRONTEND_CONFIG_CREATE_HIDDEN_V1) == 0u)
    {
        stats_.struct_size = sizeof(stats_);
    }

    ~GlfwFrontend() { shutdown(); }

    bool initialize()
    {
        if (ready_) return true;
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
        quitPublished_ = false;
        stats_.quit_requested = 0;
        if (glfwUserCount == 0u && glfwInit() != GLFW_TRUE) {
            failGlfw("GLFW initialization failed");
            return false;
        }
        ++glfwUserCount;
        ownsGlfwReference_ = true;

        if ((flags_ & TIME_FRONTEND_CONFIG_ENABLE_VIDEO_V1) != 0u) {
            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
            glfwWindowHint(GLFW_VISIBLE, visibilityRequested_ ? GLFW_TRUE : GLFW_FALSE);
            const auto width = scaledDimension(frameWidth_);
            const auto height = scaledDimension(frameHeight_);
            window_ = glfwCreateWindow(width, height, title_.c_str(), nullptr, nullptr);
            if (window_ == nullptr) {
                failGlfw("GLFW window creation failed");
                shutdown();
                return false;
            }
            glfwSetWindowUserPointer(window_, this);
            glfwSetKeyCallback(window_, &GlfwFrontend::keyCallback);
            glfwSetWindowCloseCallback(window_, &GlfwFrontend::closeCallback);
            glfwSetWindowFocusCallback(window_, &GlfwFrontend::focusCallback);
            glfwMakeContextCurrent(window_);
            glfwSwapInterval(1);
            const auto* renderer = glGetString(GL_RENDERER);
            rendererName_ = renderer != nullptr
                ? reinterpret_cast<const char*>(renderer) : "GLFW OpenGL frontend";
            stats_.renderer_flags = TIME_FRONTEND_RENDERER_ACCELERATED_V1 |
                                    TIME_FRONTEND_RENDERER_VSYNC_V1;
            windowVisible_ = glfwGetWindowAttrib(window_, GLFW_VISIBLE) == GLFW_TRUE;
        }
        ready_ = true;
        stats_.backend_ready = 1;
        stats_.window_visible = windowVisible_ ? 1 : 0;
        log("GLFW frontend initialized");
        return true;
#else
        fail("GLFW frontend backend unavailable");
        return false;
#endif
    }

    void shutdown() noexcept
    {
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
        if (window_ != nullptr) {
            glfwMakeContextCurrent(window_);
            if (texture_ != 0u) {
                glDeleteTextures(1, &texture_);
                texture_ = 0u;
            }
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (ownsGlfwReference_) {
            ownsGlfwReference_ = false;
            if (glfwUserCount > 0u) --glfwUserCount;
            if (glfwUserCount == 0u) glfwTerminate();
        }
#endif
        ready_ = false;
        windowVisible_ = false;
        stats_.backend_ready = 0;
        stats_.window_visible = 0;
    }

    bool service()
    {
        ++stats_.service_calls;
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
        if (!ready_) return false;
        glfwPollEvents();
        if (window_ != nullptr && glfwWindowShouldClose(window_) == GLFW_TRUE) publishQuit();
        return true;
#else
        return false;
#endif
    }

    bool present(const TimeFrontendFrameV1& frame)
    {
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
        if (!ready_ || window_ == nullptr || frame.pixels == nullptr ||
            frame.pixel_format != TIME_FRONTEND_PIXEL_ARGB8888_V1 ||
            frame.width <= 0 || frame.height <= 0 ||
            frame.row_stride_bytes != static_cast<std::uint32_t>(frame.width * 4)) {
            ++stats_.present_failures;
            return false;
        }
        const auto started = std::chrono::steady_clock::now();
        glfwMakeContextCurrent(window_);
        if (!ensureTexture(frame.width, frame.height)) {
            ++stats_.present_failures;
            return false;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height,
                        GL_BGRA, GL_UNSIGNED_BYTE, frame.pixels);
        ++stats_.texture_upload_count;

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window_, &framebufferWidth, &framebufferHeight);
        glViewport(0, 0, std::max(framebufferWidth, 1), std::max(framebufferHeight, 1));
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f,  1.0f);
        glEnd();
        if (glGetError() != GL_NO_ERROR) {
            fail("OpenGL frame upload or draw failed");
            ++stats_.present_failures;
            return false;
        }
        glfwSwapBuffers(window_);
        if ((flags_ & TIME_FRONTEND_CONFIG_SHOW_ON_PRESENT_V1) != 0u && !windowVisible_) {
            glfwShowWindow(window_);
            windowVisible_ = true;
            visibilityRequested_ = true;
            stats_.window_visible = 1;
        }
        ++stats_.frames_presented;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        stats_.present_duration_last_ns = elapsed;
        stats_.present_duration_high_water_ns =
            std::max(stats_.present_duration_high_water_ns, elapsed);
        return true;
#else
        (void)frame;
        ++stats_.present_failures;
        return false;
#endif
    }

    void setVisible(bool visible)
    {
        visibilityRequested_ = visible;
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
        if (window_ != nullptr) {
            if (visible) glfwShowWindow(window_); else glfwHideWindow(window_);
        }
#endif
        windowVisible_ = visible;
        stats_.window_visible = visible ? 1 : 0;
    }

    const char* backendName() const noexcept
    {
        return rendererName_.empty() ? "GLFW frontend" : rendererName_.c_str();
    }
    const char* lastError() const noexcept { return lastError_.c_str(); }
    TimeFrontendStatsV1 stats() const noexcept { return stats_; }

private:
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
    static GlfwFrontend* fromWindow(GLFWwindow* window) noexcept
    {
        return static_cast<GlfwFrontend*>(glfwGetWindowUserPointer(window));
    }

    static void keyCallback(GLFWwindow* window, int key, int, int action, int)
    {
        auto* self = fromWindow(window);
        if (self == nullptr) return;
        ++self->stats_.events_processed;
        if (action == GLFW_PRESS || action == GLFW_RELEASE) {
            const auto button = buttonForKey(key);
            if (button != 0u) {
                if (action == GLFW_PRESS) self->inputMask_ |= button;
                else self->inputMask_ &= ~button;
                self->publishInput();
                return;
            }
        }
        if (action != GLFW_PRESS || self->host_.request_control == nullptr) return;
        std::uint32_t control = 0u;
        if (key == GLFW_KEY_P) control = TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1;
        else if (key == GLFW_KEY_O) control = TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1;
        else if (key == GLFW_KEY_N) control = TIME_FRONTEND_CONTROL_SINGLE_STEP_V1;
        else if (key == GLFW_KEY_RIGHT_BRACKET) control = TIME_FRONTEND_CONTROL_SPEED_UP_V1;
        else if (key == GLFW_KEY_LEFT_BRACKET) control = TIME_FRONTEND_CONTROL_SPEED_DOWN_V1;
        if (control != 0u) self->host_.request_control(self->host_.host_context, control);
    }

    static void closeCallback(GLFWwindow* window)
    {
        auto* self = fromWindow(window);
        if (self != nullptr) {
            ++self->stats_.events_processed;
            self->publishQuit();
        }
    }

    static void focusCallback(GLFWwindow* window, int focused)
    {
        auto* self = fromWindow(window);
        if (self == nullptr) return;
        ++self->stats_.events_processed;
        if (focused == GLFW_FALSE) {
            self->inputMask_ = 0u;
            self->publishInput();
        }
    }

    static std::uint32_t buttonForKey(int key) noexcept
    {
        switch (key) {
        case GLFW_KEY_RIGHT: return TIME_FRONTEND_INPUT_RIGHT_V1;
        case GLFW_KEY_LEFT: return TIME_FRONTEND_INPUT_LEFT_V1;
        case GLFW_KEY_UP: return TIME_FRONTEND_INPUT_UP_V1;
        case GLFW_KEY_DOWN: return TIME_FRONTEND_INPUT_DOWN_V1;
        case GLFW_KEY_Z: return TIME_FRONTEND_INPUT_BUTTON1_V1;
        case GLFW_KEY_X: return TIME_FRONTEND_INPUT_BUTTON2_V1;
        case GLFW_KEY_BACKSPACE: return TIME_FRONTEND_INPUT_META1_V1;
        case GLFW_KEY_ENTER: return TIME_FRONTEND_INPUT_META2_V1;
        default: return 0u;
        }
    }

    bool ensureTexture(int width, int height)
    {
        if (texture_ != 0u && textureWidth_ == width && textureHeight_ == height) return true;
        if (texture_ != 0u) glDeleteTextures(1, &texture_);
        glGenTextures(1, &texture_);
        if (texture_ == 0u) {
            fail("OpenGL texture allocation failed");
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &texture_);
            texture_ = 0u;
            fail("OpenGL texture initialization failed");
            return false;
        }
        textureWidth_ = width;
        textureHeight_ = height;
        ++stats_.texture_recreate_count;
        return true;
    }

    static int scaledDimension(int frameDimension, std::uint32_t scale) noexcept
    {
        const auto value = static_cast<std::int64_t>(frameDimension) * scale;
        return static_cast<int>(std::clamp<std::int64_t>(value, 1, 32767));
    }
    int scaledDimension(int frameDimension) const noexcept
    {
        return scaledDimension(frameDimension, scale_);
    }

    void failGlfw(const char* fallback)
    {
        const char* description = nullptr;
        (void)glfwGetError(&description);
        fail(description != nullptr ? description : fallback);
    }
#endif

    void publishInput() const
    {
        if (host_.publish_digital_input != nullptr) {
            host_.publish_digital_input(host_.host_context, inputMask_);
        }
    }

    void publishQuit()
    {
        if (quitPublished_) return;
        quitPublished_ = true;
        stats_.quit_requested = 1;
        if (host_.request_quit != nullptr) host_.request_quit(host_.host_context);
    }

    void log(const char* message) const
    {
        if (host_.log_message != nullptr) host_.log_message(host_.host_context, 1u, message);
    }
    void fail(const char* message)
    {
        lastError_ = message != nullptr ? message : "unknown GLFW frontend error";
        log(lastError_.c_str());
    }

    TimeFrontendHostApiV1 host_{};
    std::string title_;
    std::uint32_t scale_ = 1u;
    int frameWidth_ = 160;
    int frameHeight_ = 144;
    std::uint32_t flags_ = 0u;
    std::uint32_t inputMask_ = 0u;
    bool ready_ = false;
    bool windowVisible_ = false;
    bool visibilityRequested_ = false;
    bool quitPublished_ = false;
    std::string rendererName_;
    std::string lastError_;
    TimeFrontendStatsV1 stats_{};
#if BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW
    bool ownsGlfwReference_ = false;
    GLFWwindow* window_ = nullptr;
    GLuint texture_ = 0u;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
#endif
};

void* createFrontend(const TimeFrontendHostApiV1* host, const TimeFrontendConfigV1* config)
{
    if (host == nullptr || config == nullptr ||
        host->struct_size < sizeof(TimeFrontendHostApiV1) ||
        host->abi_version != TIME_PLUGIN_ABI_VERSION_V1 ||
        config->struct_size < sizeof(TimeFrontendConfigV1)) return nullptr;
    try { return new GlfwFrontend(*host, *config); } catch (...) { return nullptr; }
}
void destroyFrontend(void* instance) { delete static_cast<GlfwFrontend*>(instance); }
int32_t initializeFrontend(void* instance)
{
    try { return instance != nullptr && static_cast<GlfwFrontend*>(instance)->initialize(); }
    catch (...) { return 0; }
}
void shutdownFrontend(void* instance)
{
    try { if (instance != nullptr) static_cast<GlfwFrontend*>(instance)->shutdown(); }
    catch (...) {}
}
int32_t serviceFrontend(void* instance)
{
    try { return instance != nullptr && static_cast<GlfwFrontend*>(instance)->service(); }
    catch (...) { return 0; }
}
int32_t presentFrontend(void* instance, const TimeFrontendFrameV1* frame)
{
    try {
        return instance != nullptr && frame != nullptr &&
            frame->struct_size >= sizeof(TimeFrontendFrameV1) &&
            static_cast<GlfwFrontend*>(instance)->present(*frame);
    } catch (...) { return 0; }
}
void setVisible(void* instance, int32_t visible)
{
    try { if (instance != nullptr) static_cast<GlfwFrontend*>(instance)->setVisible(visible != 0); }
    catch (...) {}
}
const char* backendName(const void* instance)
{
    return instance != nullptr ? static_cast<const GlfwFrontend*>(instance)->backendName() : "";
}
const char* lastError(const void* instance)
{
    return instance != nullptr ? static_cast<const GlfwFrontend*>(instance)->lastError() : "";
}
int32_t queryStats(const void* instance, TimeFrontendStatsV1* stats)
{
    if (instance == nullptr || stats == nullptr ||
        stats->struct_size < sizeof(TimeFrontendStatsV1)) return 0;
    *stats = static_cast<const GlfwFrontend*>(instance)->stats();
    return 1;
}

const TimeFrontendApiV1 frontendApi{
    sizeof(TimeFrontendApiV1), TIME_PLUGIN_ABI_VERSION_V1,
    TIME_FRONTEND_CAPABILITY_VIDEO_V1 | TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1 |
        TIME_FRONTEND_CAPABILITY_WINDOW_V1,
    &createFrontend, &destroyFrontend, &initializeFrontend, &shutdownFrontend,
    &serviceFrontend, &presentFrontend, &setVisible, &backendName, &lastError, &queryStats};

const TimePluginDescriptorV1 frontendDescriptor{
    sizeof(TimePluginDescriptorV1), TIME_PLUGIN_KIND_FRONTEND_V1,
    "bmmq.frontend.glfw", "GLFW Frontend Plugin", &frontendApi, sizeof(frontendApi)};

const TimePluginDescriptorV1* pluginAt(std::uint32_t index)
{
    return index == 0u ? &frontendDescriptor : nullptr;
}

const TimePluginModuleV1 moduleDescriptor{
    sizeof(TimePluginModuleV1), TIME_PLUGIN_ABI_VERSION_V1,
    "bmmq.module.glfw-frontend", "Proto-Time GLFW Frontend Module", 1u, &pluginAt};

} // namespace

extern "C" TIME_PLUGIN_EXPORT const TimePluginModuleV1* time_get_plugin_module_v1(void)
{
    return &moduleDescriptor;
}
