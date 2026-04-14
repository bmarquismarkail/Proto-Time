#include "SdlFrontendPluginLoader.hpp"

#include <dlfcn.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace {

class DynamicLibrary final {
public:
    explicit DynamicLibrary(void* handle) noexcept
        : handle_(handle) {}

    ~DynamicLibrary()
    {
        if (handle_ != nullptr) {
            dlclose(handle_);
        }
    }

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    [[nodiscard]] void* symbol(const char* name) const
    {
        dlerror();
        void* result = dlsym(handle_, name);
        if (const char* error = dlerror(); error != nullptr) {
            throw std::runtime_error(std::string("Unable to resolve symbol '") + name + "': " + error);
        }
        return result;
    }

private:
    void* handle_ = nullptr;
};

class LoadedSdlFrontendPlugin final : public BMMQ::ISdlFrontendPlugin {
public:
    LoadedSdlFrontendPlugin(
        std::unique_ptr<DynamicLibrary> library,
        const BMMQ::SdlFrontendPluginApiV1* api,
        BMMQ::ISdlFrontendPlugin* implementation) noexcept
        : library_(std::move(library)),
          api_(api),
          implementation_(implementation) {}

    ~LoadedSdlFrontendPlugin() override
    {
        if (implementation_ != nullptr && api_ != nullptr && api_->destroy != nullptr) {
            api_->destroy(implementation_);
            implementation_ = nullptr;
        }
    }

    void onAttach(BMMQ::MutableMachineView& view) override
    {
        implementation_->onAttach(view);
    }

    void onDetach(BMMQ::MutableMachineView& view) override
    {
        implementation_->onDetach(view);
    }

    void onMachineEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        implementation_->onMachineEvent(event, view);
    }

    void onVideoEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        implementation_->onVideoEvent(event, view);
    }

    void onAudioEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        implementation_->onAudioEvent(event, view);
    }

    std::optional<uint32_t> sampleDigitalInput(const BMMQ::MachineView& view) override
    {
        return implementation_->sampleDigitalInput(view);
    }

    void onDigitalInputEvent(const BMMQ::MachineEvent& event, const BMMQ::MachineView& view) override
    {
        implementation_->onDigitalInputEvent(event, view);
    }

    [[nodiscard]] const BMMQ::SdlFrontendConfig& config() const noexcept override
    {
        return implementation_->config();
    }

    [[nodiscard]] const BMMQ::SdlFrontendStats& stats() const noexcept override
    {
        return implementation_->stats();
    }

    [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept override
    {
        return implementation_->diagnostics();
    }

    [[nodiscard]] const std::optional<BMMQ::VideoStateView>& lastVideoState() const noexcept override
    {
        return implementation_->lastVideoState();
    }

    [[nodiscard]] const std::optional<BMMQ::AudioStateView>& lastAudioState() const noexcept override
    {
        return implementation_->lastAudioState();
    }

    [[nodiscard]] const std::optional<BMMQ::SdlAudioPreviewBuffer>& lastAudioPreview() const noexcept override
    {
        return implementation_->lastAudioPreview();
    }

    [[nodiscard]] const std::optional<BMMQ::DigitalInputStateView>& lastInputState() const noexcept override
    {
        return implementation_->lastInputState();
    }

    [[nodiscard]] const std::optional<BMMQ::SdlFrameBuffer>& lastFrame() const noexcept override
    {
        return implementation_->lastFrame();
    }

    [[nodiscard]] std::string_view lastRenderSummary() const noexcept override
    {
        return implementation_->lastRenderSummary();
    }

    [[nodiscard]] bool windowVisible() const noexcept override
    {
        return implementation_->windowVisible();
    }

    [[nodiscard]] bool windowVisibilityRequested() const noexcept override
    {
        return implementation_->windowVisibilityRequested();
    }

    void requestWindowVisibility(bool visible) override
    {
        implementation_->requestWindowVisibility(visible);
    }

    bool serviceFrontend() override
    {
        return implementation_->serviceFrontend();
    }

    void setQueuedDigitalInputMask(uint32_t pressedMask) override
    {
        implementation_->setQueuedDigitalInputMask(pressedMask);
    }

    void clearQueuedDigitalInputMask() override
    {
        implementation_->clearQueuedDigitalInputMask();
    }

    [[nodiscard]] std::optional<uint32_t> queuedDigitalInputMask() const noexcept override
    {
        return implementation_->queuedDigitalInputMask();
    }

    void pressButton(BMMQ::InputButton button) override
    {
        implementation_->pressButton(button);
    }

    void releaseButton(BMMQ::InputButton button) override
    {
        implementation_->releaseButton(button);
    }

    [[nodiscard]] bool isButtonPressed(BMMQ::InputButton button) const noexcept override
    {
        return implementation_->isButtonPressed(button);
    }

    void clearQuitRequest() noexcept override
    {
        implementation_->clearQuitRequest();
    }

    [[nodiscard]] bool quitRequested() const noexcept override
    {
        return implementation_->quitRequested();
    }

    [[nodiscard]] std::string_view lastHostEventSummary() const noexcept override
    {
        return implementation_->lastHostEventSummary();
    }

    [[nodiscard]] std::string_view lastBackendError() const noexcept override
    {
        return implementation_->lastBackendError();
    }

    [[nodiscard]] std::string backendStatusSummary() const override
    {
        return implementation_->backendStatusSummary();
    }

    [[nodiscard]] bool handleHostEvent(const BMMQ::SdlFrontendHostEvent& event) override
    {
        return implementation_->handleHostEvent(event);
    }

    [[nodiscard]] std::string_view backendName() const noexcept override
    {
        return implementation_->backendName();
    }

    [[nodiscard]] bool backendReady() const noexcept override
    {
        return implementation_->backendReady();
    }

    [[nodiscard]] bool audioOutputReady() const noexcept override
    {
        return implementation_->audioOutputReady();
    }

    [[nodiscard]] std::size_t bufferedAudioSamples() const noexcept override
    {
        return implementation_->bufferedAudioSamples();
    }

    [[nodiscard]] uint32_t queuedAudioBytes() const noexcept override
    {
        return implementation_->queuedAudioBytes();
    }

    [[nodiscard]] bool tryInitializeBackend() override
    {
        return implementation_->tryInitializeBackend();
    }

    [[nodiscard]] std::size_t pumpBackendEvents() override
    {
        return implementation_->pumpBackendEvents();
    }

private:
    std::unique_ptr<DynamicLibrary> library_;
    const BMMQ::SdlFrontendPluginApiV1* api_ = nullptr;
    BMMQ::ISdlFrontendPlugin* implementation_ = nullptr;
};

[[nodiscard]] std::string formatPluginPathError(const std::filesystem::path& pluginPath, const std::string& detail)
{
    return "Unable to load SDL frontend plugin '" + pluginPath.string() + "': " + detail;
}

} // namespace

namespace BMMQ {

std::filesystem::path defaultSdlFrontendPluginPath(const std::filesystem::path& executablePath)
{
    std::filesystem::path resolved = executablePath.empty()
        ? std::filesystem::current_path() / "timeEmulator"
        : executablePath;
    if (!resolved.is_absolute()) {
        resolved = std::filesystem::absolute(resolved);
    }
    return resolved.parent_path() / kDefaultSdlFrontendPluginFilename;
}

std::unique_ptr<ISdlFrontendPlugin> loadSdlFrontendPlugin(
    const std::filesystem::path& pluginPath,
    const SdlFrontendConfig& config)
{
    void* handle = dlopen(pluginPath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        throw std::runtime_error(formatPluginPathError(pluginPath, error != nullptr ? error : "unknown error"));
    }

    auto library = std::make_unique<DynamicLibrary>(handle);
    const auto getApi = reinterpret_cast<GetSdlFrontendPluginApiV1Fn>(
        library->symbol(kSdlFrontendPluginApiEntryPoint));
    if (getApi == nullptr) {
        throw std::runtime_error(formatPluginPathError(pluginPath, "factory entrypoint is null"));
    }

    const SdlFrontendPluginApiV1* api = getApi();
    if (api == nullptr) {
        throw std::runtime_error(formatPluginPathError(pluginPath, "factory API is null"));
    }
    if (api->structSize != sizeof(SdlFrontendPluginApiV1)) {
        throw std::runtime_error(formatPluginPathError(pluginPath, "factory API size mismatch"));
    }
    if (api->apiVersion != kSdlFrontendPluginApiVersion) {
        throw std::runtime_error(formatPluginPathError(pluginPath, "factory API version mismatch"));
    }
    if (api->create == nullptr || api->destroy == nullptr) {
        throw std::runtime_error(formatPluginPathError(pluginPath, "factory API is incomplete"));
    }

    ISdlFrontendPlugin* implementation = api->create(&config);
    if (implementation == nullptr) {
        throw std::runtime_error(formatPluginPathError(pluginPath, "plugin factory returned null"));
    }

    return std::make_unique<LoadedSdlFrontendPlugin>(std::move(library), api, implementation);
}

} // namespace BMMQ
