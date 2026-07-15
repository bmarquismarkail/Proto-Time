#include "DynamicPluginModule.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "machine/AudioService.hpp"
#include "machine/DebugSnapshotService.hpp"
#include "machine/Machine.hpp"
#include "machine/InputService.hpp"
#include "machine/TimingService.hpp"
#include "machine/VideoService.hpp"
#include "machine/plugins/AudioOutput.hpp"
#include "machine/plugins/audio_output/DummyAudioOutput.hpp"
#include "machine/plugins/audio_output/FileAudioOutput.hpp"
#include "machine/plugins/sdl_frontend/SdlAudioOutput.hpp"
#include "machine/plugins/abi/TimePluginAbi.h"

namespace BMMQ::Plugin {
namespace {

[[nodiscard]] ExecutionBackend mapBackend(std::uint32_t value)
{
    switch (value) {
    case TIME_EXECUTION_BACKEND_BASELINE_V1: return ExecutionBackend::Baseline;
    case TIME_EXECUTION_BACKEND_CACHED_BLOCK_V1: return ExecutionBackend::CachedBlock;
    case TIME_EXECUTION_BACKEND_PORTABLE_IR_V1: return ExecutionBackend::PortableIr;
    case TIME_EXECUTION_BACKEND_NATIVE_EXPERIMENTAL_V1: return ExecutionBackend::NativeExperimental;
    default: throw std::runtime_error("C executor policy returned invalid backend");
    }
}

[[nodiscard]] ExecutionGuarantee mapGuarantee(std::uint32_t value)
{
    switch (value) {
    case TIME_EXECUTION_GUARANTEE_BASELINE_FAITHFUL_V1: return ExecutionGuarantee::BaselineFaithful;
    case TIME_EXECUTION_GUARANTEE_VISIBLE_STATE_PRESERVING_V1: return ExecutionGuarantee::VisibleStatePreserving;
    case TIME_EXECUTION_GUARANTEE_EXPERIMENTAL_V1: return ExecutionGuarantee::Experimental;
    default: throw std::runtime_error("C executor policy returned invalid guarantee");
    }
}

[[nodiscard]] RuntimeCapabilityProfile mapCapabilities(std::uint32_t value)
{
    constexpr std::uint32_t known = TIME_RUNTIME_CAPABILITY_INTERCEPTION_V1 |
        TIME_RUNTIME_CAPABILITY_TRANSLATION_V1 |
        TIME_RUNTIME_CAPABILITY_INVALIDATION_V1 |
        TIME_RUNTIME_CAPABILITY_OPTIMIZATION_METADATA_V1;
    if ((value & ~known) != 0u) {
        throw std::runtime_error("C executor policy returned unknown capability bits");
    }
    return {
        (value & TIME_RUNTIME_CAPABILITY_INTERCEPTION_V1) != 0u,
        (value & TIME_RUNTIME_CAPABILITY_TRANSLATION_V1) != 0u,
        (value & TIME_RUNTIME_CAPABILITY_INVALIDATION_V1) != 0u,
        (value & TIME_RUNTIME_CAPABILITY_OPTIMIZATION_METADATA_V1) != 0u,
    };
}

[[nodiscard]] TimeExecutionObservationV1 makeObservation(
    const FetchBlock& block, const CpuFeedback& feedback) noexcept
{
    std::uint32_t flags = 0u;
    if (feedback.isControlFlow) flags |= TIME_EXECUTION_OBSERVATION_CONTROL_FLOW_V1;
    if (feedback.segmentBoundaryHint) flags |= TIME_EXECUTION_OBSERVATION_SEGMENT_BOUNDARY_V1;
    return TimeExecutionObservationV1{
        sizeof(TimeExecutionObservationV1), block.getbaseAddress(),
        static_cast<std::uint16_t>(feedback.pcBefore),
        static_cast<std::uint16_t>(feedback.pcAfter), feedback.retiredCycles, flags};
}

} // namespace

struct DynamicPluginModule::State {
    struct ExecutorEntry {
        std::string id;
        std::string displayName;
        const TimeExecutorPolicyApiV1* api = nullptr;
    };

    struct FrontendEntry {
        std::string id;
        std::string displayName;
        const TimeFrontendApiV1* api = nullptr;
    };

    ~State() {
        if (handle != nullptr) dlclose(handle);
    }

    void* handle = nullptr;
    std::string moduleId;
    std::string moduleDisplayName;
    std::vector<ExecutorEntry> executors;
    std::vector<FrontendEntry> frontends;
};

namespace {

class CExecutorPolicyAdapter final : public IExecutorPolicyPlugin {
public:
    CExecutorPolicyAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                           const DynamicPluginModule::State::ExecutorEntry& entry,
                           void* instance)
        : state_(std::move(state)), api_(entry.api), instance_(instance),
          metadata_{sizeof(PluginMetadata), entry.id, entry.displayName,
                    PluginKind::ExecutorPolicy, kHostAbiVersion}
    {
    }

    ~CExecutorPolicyAdapter() override {
        if (instance_ != nullptr) api_->destroy(instance_);
    }

    std::unique_ptr<IExecutorPolicyPlugin> clone() const override {
        void* instance = api_->create(&hostApi());
        if (instance == nullptr) throw std::runtime_error("C executor policy clone failed");
        DynamicPluginModule::State::ExecutorEntry entry{metadata_.id, metadata_.displayName, api_};
        try {
            return std::make_unique<CExecutorPolicyAdapter>(state_, entry, instance);
        } catch (...) {
            api_->destroy(instance);
            throw;
        }
    }

    const PluginMetadata& metadata() const override { return metadata_; }
    ExecutionBackend backend() const override { return mapBackend(api_->backend(instance_)); }
    ExecutionGuarantee guarantee() const override { return mapGuarantee(api_->guarantee(instance_)); }
    RuntimeCapabilityProfile requiredCapabilities() const override {
        return mapCapabilities(api_->required_capabilities(instance_));
    }
    bool shouldRecord(const FetchBlock& block, const CpuFeedback& feedback) const override {
        const auto observation = makeObservation(block, feedback);
        return api_->should_record(instance_, &observation) != 0;
    }
    bool shouldSegment(const FetchBlock& block, const CpuFeedback& feedback) const override {
        const auto observation = makeObservation(block, feedback);
        return api_->should_segment(instance_, &observation) != 0;
    }

    static const TimeHostApiV1& hostApi() noexcept {
        static const TimeHostApiV1 api{sizeof(TimeHostApiV1), TIME_PLUGIN_ABI_VERSION_V1,
                                       nullptr, nullptr};
        return api;
    }

private:
    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeExecutorPolicyApiV1* api_ = nullptr;
    void* instance_ = nullptr;
    PluginMetadata metadata_;
};

class CFrontendAdapter final : public IFrontendPlugin {
public:
    CFrontendAdapter(std::shared_ptr<DynamicPluginModule::State> state,
                     const DynamicPluginModule::State::FrontendEntry& entry,
                     const SdlFrontendConfig& config)
        : state_(std::move(state)), api_(entry.api), id_(entry.id),
          displayName_(entry.displayName), config_(config)
    {
        hostApi_ = TimeFrontendHostApiV1{
            sizeof(TimeFrontendHostApiV1), TIME_PLUGIN_ABI_VERSION_V1, this,
            &CFrontendAdapter::logMessage, &CFrontendAdapter::publishInput,
            &CFrontendAdapter::requestQuit, &CFrontendAdapter::requestControl};
        std::uint32_t flags = 0u;
        if (config_.enableVideo) flags |= TIME_FRONTEND_CONFIG_ENABLE_VIDEO_V1;
        if (config_.enableInput) flags |= TIME_FRONTEND_CONFIG_ENABLE_INPUT_V1;
        if (config_.createHiddenWindowOnInitialize) flags |= TIME_FRONTEND_CONFIG_CREATE_HIDDEN_V1;
        if (config_.showWindowOnPresent) flags |= TIME_FRONTEND_CONFIG_SHOW_ON_PRESENT_V1;
        const TimeFrontendConfigV1 cConfig{
            sizeof(TimeFrontendConfigV1), config_.windowTitle.c_str(), config_.windowScale,
            config_.frameWidth, config_.frameHeight, flags};
        try { instance_ = api_->create(&hostApi_, &cConfig); }
        catch (...) { throw std::runtime_error("C frontend factory threw across the ABI"); }
        if (instance_ == nullptr) throw std::runtime_error("C frontend factory returned null");
        presenter_ = std::make_unique<Presenter>(*this);
    }

    ~CFrontendAdapter() override
    {
        detachServices();
        if (instance_ != nullptr) {
            try { api_->shutdown(instance_); } catch (...) {}
            try { api_->destroy(instance_); } catch (...) {}
            instance_ = nullptr;
        }
    }

    std::string_view id() const override { return id_; }
    std::string_view displayName() const override { return displayName_; }
    InputPluginCapabilities capabilities() const noexcept override
    {
        return {.pollingSafe = true, .eventPumpSafe = true, .deterministic = true,
                .supportsDigital = true,
                .fixedLogicalLayout = true, .hotSwapSafe = true, .headlessSafe = false};
    }
    std::string_view name() const noexcept override { return backendName(); }
    bool open() override { return true; }
    void close() noexcept override {}
    std::string_view lastError() const noexcept override { return lastBackendError(); }

    void onAttach(MutableMachineView& view) override
    {
        ++stats_.attachCount;
        audioService_ = &view.audioService();
        videoService_ = &view.videoService();
        timingService_ = &view.timingService();
        inputService_ = &view.inputService();
        if (config_.enableInput) {
            (void)inputService_->attachExternalAdapter(*this);
            (void)inputService_->configureMappingProfile("sdl-default");
            (void)inputService_->resume();
        }
        if (config_.enableVideo) {
            const bool configured = videoService_->configure({
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
                .mailboxDepthFrames = 1,
            });
            videoService_->setPresenterPolicy(config_.videoPresenterPolicy);
            const bool presenterConfigured = videoService_->configurePresenter({
                .windowTitle = config_.windowTitle,
                .scale = static_cast<int>(std::min<std::uint32_t>(
                    std::max(config_.windowScale, 1u),
                    static_cast<std::uint32_t>(std::numeric_limits<int>::max()))),
                .frameWidth = std::max(config_.frameWidth, 1),
                .frameHeight = std::max(config_.frameHeight, 1),
                .mode = VideoPresenterMode::Hardware,
                .createHiddenWindowOnOpen = config_.createHiddenWindowOnInitialize,
                .showWindowOnPresent = config_.showWindowOnPresent,
            });
            auto presenter = std::move(presenter_);
            const bool attached = configured && presenterConfigured &&
                videoService_->attachPresenter(std::move(presenter));
            const bool resumed = !config_.autoInitializeBackend ||
                (attached && videoService_->resume());
            if (!attached || !resumed) {
                diagnostics_.push_back("frontend: video service initialization failed");
            }
        } else if (config_.autoInitializeBackend) {
            (void)tryInitializeBackend();
        }
        openAudioOutput();
    }

    void onDetach(MutableMachineView&) override
    {
        ++stats_.detachCount;
        flushAudioBatch();
        detachServices();
    }

    void onMachineEvent(const MachineEvent&, const MachineView&) override {}

    void onVideoEvent(const MachineEvent& event, const MachineView& view) override
    {
        videoEvents_.fetch_add(1u, std::memory_order_relaxed);
        if (!config_.enableVideo || videoService_ == nullptr) return;
        if (event.type == MachineEventType::RomLoaded) {
            (void)videoService_->submitRealtimeVideoPacket(event, {});
            return;
        }
        if (event.type != MachineEventType::VBlank) return;
        const auto started = std::chrono::steady_clock::now();
        auto submission = view.realtimeVideoPacket({
            .frameWidth = std::max(config_.frameWidth, 1),
            .frameHeight = std::max(config_.frameHeight, 1),
        });
        if (submission.has_value() &&
            videoService_->submitRealtimeVideoPacket(event, std::move(*submission))) {
            framesPrepared_.fetch_add(1u, std::memory_order_relaxed);
            videoPacketsAccepted_.fetch_add(1u, std::memory_order_relaxed);
            videoPacketsBuiltOutsideLock_.fetch_add(1u, std::memory_order_relaxed);
        } else {
            videoPacketsSkipped_.fetch_add(1u, std::memory_order_relaxed);
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        videoBuildLastNs_.store(elapsed, std::memory_order_relaxed);
        atomicMax(videoBuildHighWaterNs_, elapsed);
        videoBuildSamples_.fetch_add(1u, std::memory_order_relaxed);
    }

    void onAudioEvent(const MachineEvent& event, const MachineView& view) override
    {
        if (!config_.enableAudio || audioService_ == nullptr ||
            event.type != MachineEventType::AudioFrameReady) return;
        audioEvents_.fetch_add(1u, std::memory_order_relaxed);
        if (auto packet = view.realtimeAudioPacket(); packet.has_value() &&
            packet->contractVersion == RealtimeAudioPacket::kContractVersion) {
            audioPacketsAccepted_.fetch_add(1u, std::memory_order_relaxed);
            if (config_.audioBatchChunks <= 1u) {
                audioService_->appendRecentPcm(packet->pcmSamples, packet->frameCounter);
                audioBatchFlushCount_.fetch_add(1u, std::memory_order_relaxed);
                audioBatchFlushLast_.store(packet->pcmSamples.size(), std::memory_order_relaxed);
                atomicMinNonZero(audioBatchFlushMin_, packet->pcmSamples.size());
                atomicMax(audioBatchFlushMax_, packet->pcmSamples.size());
                audioBatchPacketsAccumulated_.fetch_add(1u, std::memory_order_relaxed);
                audioBatchPacketsFlushed_.fetch_add(1u, std::memory_order_relaxed);
            } else {
                if (!audioBatchSamples_.empty() &&
                    (audioBatchSampleRate_ != packet->sampleRate ||
                     audioBatchChannels_ != packet->channelCount)) flushAudioBatch();
                audioBatchSampleRate_ = packet->sampleRate;
                audioBatchChannels_ = packet->channelCount;
                audioBatchFrameCounter_ = packet->frameCounter;
                audioBatchSamples_.insert(audioBatchSamples_.end(),
                    packet->pcmSamples.begin(), packet->pcmSamples.end());
                ++audioBatchPackets_;
                audioBatchPacketsAccumulated_.fetch_add(1u, std::memory_order_relaxed);
                audioBatchCurrentSamples_.store(audioBatchSamples_.size(), std::memory_order_relaxed);
                if (audioBatchPackets_ >= config_.audioBatchChunks) flushAudioBatch();
            }
            if (config_.retainDebugSnapshots || debugSnapshotService_ != nullptr) {
                SdlAudioPreviewBuffer preview;
                preview.sampleRate = static_cast<int>(packet->sampleRate);
                preview.channels = packet->channelCount;
                const auto count = std::min<std::size_t>(packet->pcmSamples.size(),
                    static_cast<std::size_t>(std::max(config_.audioPreviewSampleCount, 0)));
                preview.samples.assign(packet->pcmSamples.end() - static_cast<std::ptrdiff_t>(count),
                                       packet->pcmSamples.end());
                lastAudioPreview_ = std::move(preview);
            }
            return;
        }
        audioPacketsSkipped_.fetch_add(1u, std::memory_order_relaxed);
        if (auto state = view.audioState(); state.has_value()) {
            if (config_.retainDebugSnapshots || debugSnapshotService_ != nullptr) {
                lastAudioState_ = *state;
            }
            audioService_->appendRecentPcm(state->pcmSamples, state->frameCounter);
        }
    }

    std::optional<uint32_t> sampleDigitalInput(const MachineView&) override
    {
        if (!config_.enableInput) return std::nullopt;
        const auto value = inputMask_.load(std::memory_order_acquire);
        ++inputPolls_;
        inputSamplesProvided_.fetch_add(1u, std::memory_order_relaxed);
        return value >= 0 ? std::optional<uint32_t>(static_cast<uint32_t>(value))
                          : std::optional<uint32_t>(0u);
    }

    std::optional<InputButtonMask> sampleDigitalInput() override
    {
        if (!config_.enableInput) return std::nullopt;
        const auto value = inputMask_.load(std::memory_order_acquire);
        ++inputPolls_;
        inputSamplesProvided_.fetch_add(1u, std::memory_order_relaxed);
        return value >= 0
            ? std::optional<InputButtonMask>(static_cast<InputButtonMask>(value & 0xff))
            : std::optional<InputButtonMask>(static_cast<InputButtonMask>(0u));
    }

    void onDigitalInputEvent(const MachineEvent&, const MachineView& view) override
    {
        inputEvents_.fetch_add(1u, std::memory_order_relaxed);
        if (config_.retainDebugSnapshots || debugSnapshotService_ != nullptr) {
            lastInputState_ = view.digitalInputState();
        }
    }

    const SdlFrontendConfig& config() const noexcept override { return config_; }

    SdlFrontendStats stats() const noexcept override
    {
        auto result = stats_;
        result.videoEvents = videoEvents_.load(std::memory_order_relaxed);
        result.framesPrepared = framesPrepared_.load(std::memory_order_relaxed);
        result.videoRealtimePacketsAccepted = videoPacketsAccepted_.load(std::memory_order_relaxed);
        result.videoRealtimePacketsSkipped = videoPacketsSkipped_.load(std::memory_order_relaxed);
        result.videoRealtimePacketsBuiltOutsideLock =
            videoPacketsBuiltOutsideLock_.load(std::memory_order_relaxed);
        result.videoFrameBuildDurationLastNanos = videoBuildLastNs_.load(std::memory_order_relaxed);
        result.videoFrameBuildDurationHighWaterNanos =
            videoBuildHighWaterNs_.load(std::memory_order_relaxed);
        result.videoFrameBuildDurationSampleCount = videoBuildSamples_.load(std::memory_order_relaxed);
        result.audioEvents = audioEvents_.load(std::memory_order_relaxed);
        result.audioRealtimePacketsAccepted = audioPacketsAccepted_.load(std::memory_order_relaxed);
        result.audioRealtimePacketsSkipped = audioPacketsSkipped_.load(std::memory_order_relaxed);
        result.audioBatchFlushCount = audioBatchFlushCount_.load(std::memory_order_relaxed);
        result.audioBatchFlushSamplesLast = audioBatchFlushLast_.load(std::memory_order_relaxed);
        result.audioBatchFlushSamplesMin = audioBatchFlushMin_.load(std::memory_order_relaxed);
        result.audioBatchFlushSamplesMax = audioBatchFlushMax_.load(std::memory_order_relaxed);
        result.audioBatchPacketsAccumulated =
            audioBatchPacketsAccumulated_.load(std::memory_order_relaxed);
        result.audioBatchPacketsFlushed = audioBatchPacketsFlushed_.load(std::memory_order_relaxed);
        result.audioBatchCurrentSamples = audioBatchCurrentSamples_.load(std::memory_order_relaxed);
        result.inputEvents = inputEvents_.load(std::memory_order_relaxed);
        result.inputSamplesProvided = inputSamplesProvided_.load(std::memory_order_relaxed);
        result.inputPolls = inputPolls_.load(std::memory_order_relaxed);
        result.quitRequests = quitRequestCount_.load(std::memory_order_relaxed);
        TimeFrontendStatsV1 backend{};
        backend.struct_size = sizeof(TimeFrontendStatsV1);
        bool haveBackendStats = false;
        if (instance_ != nullptr) {
            try { haveBackendStats = api_->query_stats(instance_, &backend) != 0; }
            catch (...) { haveBackendStats = false; }
        }
        if (haveBackendStats) {
            result.renderAttempts = backend.frames_presented + backend.present_failures;
            result.framesPresented = backend.frames_presented;
            result.videoPresentCount = backend.frames_presented;
            result.videoPresenterTextureRecreateCount = backend.texture_recreate_count;
            result.videoPresenterTextureUploadCount = backend.texture_upload_count;
            result.videoPresenterRenderCount = backend.frames_presented;
            result.videoPresenterRendererFlags = backend.renderer_flags;
            result.videoPresenterPresentDurationLastNanos = backend.present_duration_last_ns;
            result.videoPresenterPresentDurationHighWaterNanos = backend.present_duration_high_water_ns;
            result.videoPresenterPresentDurationSampleCount = backend.frames_presented;
        }
        syncServiceStats(result);
        return result;
    }

    const std::vector<std::string>& diagnostics() const noexcept override { return diagnostics_; }
    const std::optional<VideoDebugFrameModel>& lastVideoDebugModel() const noexcept override {
        return lastVideoDebugModel_;
    }
    const std::optional<AudioStateView>& lastAudioState() const noexcept override { return lastAudioState_; }
    const std::optional<SdlAudioPreviewBuffer>& lastAudioPreview() const noexcept override {
        return lastAudioPreview_;
    }
    const std::optional<DigitalInputStateView>& lastInputState() const noexcept override {
        return lastInputState_;
    }
    const std::optional<SdlFrameBuffer>& lastFrame() const noexcept override { return lastFrame_; }
    std::string_view lastRenderSummary() const noexcept override { return lastRenderSummary_; }
    bool windowVisible() const noexcept override { return backendStats().window_visible != 0; }
    bool windowVisibilityRequested() const noexcept override { return visibilityRequested_; }
    void requestWindowVisibility(bool visible) override
    {
        visibilityRequested_ = visible;
        if (instance_ != nullptr) {
            try { api_->set_window_visible(instance_, visible ? 1 : 0); } catch (...) {}
        }
    }

    bool serviceFrontend() override
    {
        if (instance_ == nullptr) return false;
        if (!backendReady() && !tryInitializeBackend()) return true;
        if (audioOutput_ != nullptr) audioOutput_->service();
        bool serviced = false;
        try { serviced = api_->service(instance_) != 0; } catch (...) { serviced = false; }
        if (videoService_ != nullptr && videoService_->hasPendingRealtimeFrame()) {
            (void)videoService_->presentOneFrame();
        }
        return serviced;
    }

    void setQueuedDigitalInputMask(uint32_t mask) override
    {
        inputMask_.store(static_cast<std::int32_t>(mask & 0xffu), std::memory_order_release);
    }
    void clearQueuedDigitalInputMask() override { inputMask_.store(-1, std::memory_order_release); }
    std::optional<uint32_t> queuedDigitalInputMask() const noexcept override
    {
        const auto value = inputMask_.load(std::memory_order_acquire);
        return value >= 0 ? std::optional<uint32_t>(static_cast<uint32_t>(value)) : std::nullopt;
    }
    void pressButton(InputButton button) override { setButton(button, true); }
    void releaseButton(InputButton button) override { setButton(button, false); }
    bool isButtonPressed(InputButton button) const noexcept override
    {
        const auto value = inputMask_.load(std::memory_order_acquire);
        return value >= 0 && (value & inputButtonMask(button)) != 0;
    }
    void clearQuitRequest() noexcept override { quitRequested_.store(false, std::memory_order_release); }
    bool quitRequested() const noexcept override { return quitRequested_.load(std::memory_order_acquire); }
    std::string_view lastHostEventSummary() const noexcept override { return lastHostEventSummary_; }
    std::string_view lastBackendError() const noexcept override
    {
        refreshBackendStrings();
        return lastBackendError_;
    }
    std::string backendStatusSummary() const override
    {
        refreshBackendStrings();
        return backendReady() ? backendName_ + " ready" : backendName_ + ": " + lastBackendError_;
    }
    bool handleHostEvent(const SdlFrontendHostEvent& event) override
    {
        lastHostEventSummary_ = "frontend host event";
        if (event.type == SdlFrontendHostEventType::Quit) {
            requestQuit(&*this);
            return true;
        }
        const auto button = buttonForHostKey(event.key);
        if (button.has_value() && (event.type == SdlFrontendHostEventType::KeyDown ||
                                  event.type == SdlFrontendHostEventType::KeyUp)) {
            setButton(*button, event.type == SdlFrontendHostEventType::KeyDown);
            return true;
        }
        if (event.type == SdlFrontendHostEventType::KeyDown && !event.repeat) {
            handleControlKey(event.key);
            return event.key != SdlFrontendHostKey::Unknown;
        }
        return false;
    }
    std::string_view backendName() const noexcept override
    {
        refreshBackendStrings();
        return backendName_;
    }
    bool backendReady() const noexcept override { return backendStats().backend_ready != 0; }
    bool audioOutputReady() const noexcept override { return audioOutput_ != nullptr && audioOutput_->ready(); }
    std::size_t bufferedAudioSamples() const noexcept override
    {
        return audioService_ != nullptr ? audioService_->engine().bufferedSamples() : 0u;
    }
    bool audioQueueBackpressureActive() const noexcept override
    {
        if (audioService_ == nullptr || !audioOutputReady()) return false;
        const auto capacity = audioService_->engine().bufferCapacitySamples();
        return capacity != 0u && audioService_->engine().bufferedSamples() >= (capacity * 9u) / 10u;
    }
    uint32_t queuedAudioBytes() const noexcept override
    {
        return audioService_ != nullptr ? audioService_->engine().queuedBytes() : 0u;
    }
    bool tryInitializeBackend() override
    {
        if (instance_ == nullptr) return false;
        if (backendReady()) return true;
        ++stats_.backendInitAttempts;
        bool ok = false;
        try { ok = api_->initialize(instance_) != 0; } catch (...) { ok = false; }
        refreshBackendStrings();
        return ok;
    }
    std::size_t pumpBackendEvents() override
    {
        const auto before = backendStats().events_processed;
        if (instance_ != nullptr) {
            try { (void)api_->service(instance_); } catch (...) {}
        }
        const auto after = backendStats().events_processed;
        return static_cast<std::size_t>(after >= before ? after - before : 0u);
    }
    void setDebugSnapshotService(DebugSnapshotService* service) noexcept override { debugSnapshotService_ = service; }
    DebugSnapshotService* debugSnapshotService() const noexcept override { return debugSnapshotService_; }

private:
    template<typename T>
    static void atomicMax(std::atomic<T>& target, T value) noexcept
    {
        auto current = target.load(std::memory_order_relaxed);
        while (current < value &&
               !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
    }

    template<typename T>
    static void atomicMinNonZero(std::atomic<T>& target, T value) noexcept
    {
        auto current = target.load(std::memory_order_relaxed);
        while ((current == 0 || value < current) &&
               !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
    }

    class Presenter final : public IVideoPresenterPlugin {
    public:
        explicit Presenter(CFrontendAdapter& owner) : owner_(owner) {}
        std::string_view name() const noexcept override { return owner_.backendName(); }
        VideoPluginCapabilities capabilities() const noexcept override {
            return {.realtimeSafe = true, .frameSizePreserving = true,
                    .requiresHostThreadAffinity = true};
        }
        bool open(const VideoPresenterConfig&) override { return owner_.tryInitializeBackend(); }
        void close() noexcept override {}
        bool ready() const noexcept override { return owner_.backendReady(); }
        bool present(const VideoFramePacket& frame) noexcept override
        {
            if (owner_.instance_ == nullptr || frame.pixels.empty()) return false;
            const TimeFrontendFrameV1 cFrame{
                sizeof(TimeFrontendFrameV1), TIME_FRONTEND_PIXEL_ARGB8888_V1,
                frame.width, frame.height,
                static_cast<std::uint32_t>(frame.width * static_cast<int>(sizeof(uint32_t))),
                frame.pixels.data(), frame.generation, frame.lifecycleEpoch};
            bool ok = false;
            try { ok = owner_.api_->present(owner_.instance_, &cFrame) != 0; } catch (...) { ok = false; }
            if (ok && owner_.config_.retainLastPresentedFrame) {
                SdlFrameBuffer snapshot;
                snapshot.width = frame.width;
                snapshot.height = frame.height;
                snapshot.generation = frame.generation;
                snapshot.pixels = frame.pixels;
                owner_.lastFrame_ = std::move(snapshot);
                owner_.lastRenderSummary_ = "frontend frame presented";
            }
            return ok;
        }
        std::string_view lastError() const noexcept override { return owner_.lastBackendError(); }
        VideoPresenterDiagnostics diagnostics() const noexcept override
        {
            const auto stats = owner_.backendStats();
            VideoPresenterDiagnostics result;
            result.presentCount = stats.frames_presented;
            result.textureRecreateCount = stats.texture_recreate_count;
            result.textureUploadCount = stats.texture_upload_count;
            result.rendererName = owner_.backendName();
            result.rendererFlags = stats.renderer_flags;
            result.presenterPresentDurationLastNanos = stats.present_duration_last_ns;
            result.presenterPresentDurationHighWaterNanos = stats.present_duration_high_water_ns;
            result.presenterPresentDurationSampleCount = stats.frames_presented;
            return result;
        }
        bool windowVisible() const noexcept override { return owner_.windowVisible(); }
        void requestWindowVisibility(bool value) noexcept override { owner_.requestWindowVisibility(value); }
        bool windowVisibilityRequested() const noexcept override { return owner_.windowVisibilityRequested(); }
    private:
        CFrontendAdapter& owner_;
    };

    static void logMessage(void* context, std::uint32_t, const char* message) noexcept
    {
        if (context == nullptr || message == nullptr) return;
        try { static_cast<CFrontendAdapter*>(context)->diagnostics_.emplace_back(message); }
        catch (...) {}
    }
    static void publishInput(void* context, std::uint32_t mask)
    {
        if (context == nullptr) return;
        auto& self = *static_cast<CFrontendAdapter*>(context);
        self.inputMask_.store(static_cast<std::int32_t>(mask & 0xffu), std::memory_order_release);
    }
    static void requestQuit(void* context)
    {
        if (context == nullptr) return;
        auto& self = *static_cast<CFrontendAdapter*>(context);
        self.quitRequested_.store(true, std::memory_order_release);
        self.quitRequestCount_.fetch_add(1u, std::memory_order_relaxed);
    }
    static void requestControl(void* context, std::uint32_t action) noexcept
    {
        try {
            if (context != nullptr) static_cast<CFrontendAdapter*>(context)->handleControl(action);
        } catch (...) {}
    }

    void handleControl(std::uint32_t action)
    {
        if (timingService_ == nullptr) return;
        const auto timing = timingService_->stats();
        switch (action) {
        case TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1: timingService_->setPaused(!timing.paused); break;
        case TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1: timingService_->setThrottled(!timing.throttled); break;
        case TIME_FRONTEND_CONTROL_SINGLE_STEP_V1: timingService_->requestSingleStep(); break;
        case TIME_FRONTEND_CONTROL_SPEED_UP_V1:
            timingService_->setSpeedMultiplier(std::min(timing.speedMultiplier * 2.0, 16.0)); break;
        case TIME_FRONTEND_CONTROL_SPEED_DOWN_V1:
            timingService_->setSpeedMultiplier(std::max(timing.speedMultiplier * 0.5, 0.125)); break;
        default: break;
        }
    }

    void handleControlKey(SdlFrontendHostKey key)
    {
        if (key == SdlFrontendHostKey::Pause) handleControl(TIME_FRONTEND_CONTROL_TOGGLE_PAUSE_V1);
        else if (key == SdlFrontendHostKey::ThrottleToggle) handleControl(TIME_FRONTEND_CONTROL_TOGGLE_THROTTLE_V1);
        else if (key == SdlFrontendHostKey::SingleStep) handleControl(TIME_FRONTEND_CONTROL_SINGLE_STEP_V1);
        else if (key == SdlFrontendHostKey::SpeedUp) handleControl(TIME_FRONTEND_CONTROL_SPEED_UP_V1);
        else if (key == SdlFrontendHostKey::SpeedDown) handleControl(TIME_FRONTEND_CONTROL_SPEED_DOWN_V1);
    }

    static std::optional<InputButton> buttonForHostKey(SdlFrontendHostKey key)
    {
        switch (key) {
        case SdlFrontendHostKey::Right: return InputButton::Right;
        case SdlFrontendHostKey::Left: return InputButton::Left;
        case SdlFrontendHostKey::Up: return InputButton::Up;
        case SdlFrontendHostKey::Down: return InputButton::Down;
        case SdlFrontendHostKey::Z: return InputButton::Button1;
        case SdlFrontendHostKey::X: return InputButton::Button2;
        case SdlFrontendHostKey::Backspace: return InputButton::Meta1;
        case SdlFrontendHostKey::Return: return InputButton::Meta2;
        default: return std::nullopt;
        }
    }
    void setButton(InputButton button, bool pressed)
    {
        auto current = inputMask_.load(std::memory_order_acquire);
        if (current < 0) current = 0;
        const auto bit = static_cast<std::int32_t>(inputButtonMask(button));
        const auto next = pressed ? (current | bit) : (current & ~bit);
        inputMask_.store(next, std::memory_order_release);
        ++stats_.buttonTransitions;
    }

    std::unique_ptr<IAudioOutputBackend> makeAudioOutput() const
    {
        if (config_.audioBackend == "sdl") return std::make_unique<SdlAudioOutputBackend>();
        if (config_.audioBackend == "file") return std::make_unique<FileAudioOutputBackend>();
        if (config_.audioBackend == "dummy") return std::make_unique<DummyAudioOutputBackend>();
        return {};
    }
    void openAudioOutput()
    {
        if (!config_.enableAudio || audioService_ == nullptr) return;
        audioOutput_ = makeAudioOutput();
        if (!audioOutput_) {
            diagnostics_.push_back("frontend: unknown audio backend: " + config_.audioBackend);
            return;
        }
        const auto channels = std::max<int>(audioService_->engine().config().channelCount, 1);
        if (!audioOutput_->open(audioService_->engine(), {
                .backend = config_.audioBackend,
                .requestedSampleRate = audioService_->engine().config().sourceSampleRate,
                .callbackChunkSamples = static_cast<std::size_t>(
                    std::max(config_.audioCallbackChunkSamples, 1)) * static_cast<std::size_t>(channels),
                .readyQueueChunks = std::clamp<std::size_t>(config_.audioReadyQueueChunks, 1u, 64u),
                .channels = channels,
                .testForcedDeviceSampleRate = config_.enableAudioResamplingDiagnostics
                    ? config_.testForcedAudioDeviceSampleRate : 0,
                .filePath = config_.audioOutputFilePath,
                .appendToFile = config_.audioFileAppend,
                .audioService = audioService_,
            })) {
            diagnostics_.push_back("frontend: audio open failed: " + audioOutput_->lastError());
        }
    }
    void flushAudioBatch()
    {
        if (audioBatchSamples_.empty() || audioService_ == nullptr) return;
        audioService_->appendRecentPcm(audioBatchSamples_, audioBatchFrameCounter_);
        audioBatchFlushCount_.fetch_add(1u, std::memory_order_relaxed);
        audioBatchFlushLast_.store(audioBatchSamples_.size(), std::memory_order_relaxed);
        atomicMinNonZero(audioBatchFlushMin_, audioBatchSamples_.size());
        atomicMax(audioBatchFlushMax_, audioBatchSamples_.size());
        audioBatchPacketsFlushed_.fetch_add(audioBatchPackets_, std::memory_order_relaxed);
        audioBatchSamples_.clear();
        audioBatchPackets_ = 0u;
        audioBatchCurrentSamples_.store(0u, std::memory_order_relaxed);
    }

    void detachServices() noexcept
    {
        if (audioOutput_ != nullptr) {
            audioOutput_->close();
            audioOutput_.reset();
        }
        if (videoService_ != nullptr) {
            (void)videoService_->pause();
            (void)videoService_->detachPresenter();
        }
        if (instance_ != nullptr) {
            try { api_->shutdown(instance_); } catch (...) {}
        }
        audioService_ = nullptr;
        videoService_ = nullptr;
        timingService_ = nullptr;
        if (inputService_ != nullptr) {
            (void)inputService_->detachAdapter(inputService_->currentGeneration() + 1u);
            inputService_ = nullptr;
        }
    }

    TimeFrontendStatsV1 backendStats() const noexcept
    {
        TimeFrontendStatsV1 result{};
        result.struct_size = sizeof(TimeFrontendStatsV1);
        if (instance_ != nullptr) {
            try { (void)api_->query_stats(instance_, &result); } catch (...) {}
        }
        return result;
    }
    void refreshBackendStrings() const noexcept
    {
        if (instance_ == nullptr) return;
        try {
            const char* name = api_->backend_name(instance_);
            const char* error = api_->last_error(instance_);
            backendName_ = name != nullptr ? name : "frontend";
            lastBackendError_ = error != nullptr ? error : "";
        } catch (...) {
            lastBackendError_ = "frontend callback threw across the C ABI";
        }
    }
    void syncServiceStats(SdlFrontendStats& result) const noexcept
    {
        if (videoService_ != nullptr) {
            const auto video = videoService_->diagnostics();
            result.videoFramesPublished = video.publishedFrameCount;
            result.videoMailboxDepth = video.mailboxDepth;
            result.videoMailboxHighWaterFrames = video.mailboxHighWaterMark;
            result.videoMailboxStaleDropCount = video.staleFrameDropCount;
            result.videoMailboxOverwriteCount = video.overwriteCount;
            result.videoLastPublishedGeneration = video.lastPublishedGeneration;
            result.videoLastPresentedGeneration = video.lastPresentedGeneration;
            result.videoPresentFreshFrameCount = video.presentFromFreshFrameCount;
            result.videoPresentFallbackCount = video.presentFallbackCount;
            result.videoStaleEpochDropCount = video.staleEpochDropCount;
            result.videoLifecycleEpochBumpCount = video.lifecycleEpochBumpCount;
            result.videoLifecycleEpoch = video.lifecycleEpoch;
            result.videoFrameAgeLastNs = video.frameAgeLastNs;
            result.videoFrameAgeHighWaterNs = video.frameAgeHighWaterNs;
            result.videoPresenterRendererName = video.presenterRendererName;
            result.videoPresenterRendererAccelerated =
                (video.presenterRendererFlags & 0x2u) != 0u;
        }
        if (audioService_ != nullptr) {
            const auto engine = audioService_->engine().stats();
            const auto transport = audioService_->transportStats();
            result.audioSourceSampleRate = audioService_->engine().config().sourceSampleRate;
            result.audioDeviceSampleRate = audioService_->engine().config().deviceSampleRate;
            result.audioRingBufferCapacitySamples = audioService_->engine().bufferCapacitySamples();
            const auto device = audioOutput_ != nullptr
                ? audioOutput_->deviceInfo() : AudioOutputDeviceInfo{};
            result.audioCallbackChunkSamples = device.callbackChunkSamples;
            result.audioBufferedHighWaterSamples = engine.bufferedHighWaterSamples;
            result.audioCallbackCount = engine.callbackCount;
            result.audioSamplesDelivered = engine.samplesDelivered;
            result.audioUnderrunCount = engine.underrunCount;
            result.audioSilenceSamplesFilled = engine.silenceSamplesFilled;
            result.audioOverrunDropCount = engine.overrunDropCount;
            result.audioDroppedSamples = engine.droppedSamples;
            result.audioResamplingActive = engine.resamplingActive;
            result.audioResampleRatio = engine.resampleRatio;
            result.audioSourceSamplesPushed = engine.sourceSamplesPushed;
            result.audioAppendCallCount = engine.appendCallCount;
            result.audioAppendSamplesRequested = engine.appendSamplesRequested;
            result.audioAppendSamplesAccepted = engine.appendSamplesAccepted;
            result.audioAppendSamplesRejected = engine.appendSamplesRejected;
            result.audioAppendSamplesTruncated = engine.appendSamplesTruncated;
            result.audioAppendBufferedSamplesLast = engine.appendBufferedSamplesLast;
            result.audioResampleSourceSamplesConsumed = engine.sourceSamplesConsumed;
            result.audioResampleOutputSamplesProduced = engine.outputSamplesProduced;
            result.audioPipelineCapacitySkipCount = engine.pipelineCapacitySkipCount;
            result.audioReadyQueueDepth = transport.readyQueueDepth;
            result.audioTransportConfiguredReadyQueueChunks = transport.configuredReadyQueueChunks;
            result.audioTransportPrefillTargetChunks = transport.prefillTargetChunks;
            result.audioTransportReadyQueueCapacityChunks = transport.readyQueueCapacityChunks;
            result.audioTransportReadyQueueUsableChunks = transport.readyQueueUsableChunks;
            result.audioReadyQueueHighWaterChunks = transport.readyQueueHighWaterChunks;
            result.audioReadyQueueLowWaterChunks = transport.readyQueueLowWaterChunks;
            result.audioReadyQueueEmptyCount = transport.readyQueueEmptyCount;
            result.audioTransportDrainCallbackCount = transport.drainCallbackCount;
            result.audioTransportDrainRequestedSamples = transport.drainRequestedSamples;
            result.audioTransportDrainReadySamples = transport.drainReadySamples;
            result.audioTransportUnderrunCount = transport.underrunCount;
            result.audioTransportSilenceSamplesFilled = transport.silenceSamplesFilled;
            result.audioTransportWorkerWakeCount = transport.workerWakeCount;
            result.audioTransportWorkerCallbackWakeCount = transport.workerCallbackWakeCount;
            result.audioTransportWorkerEmulationWakeCount = transport.workerEmulationWakeCount;
            result.audioTransportWorkerTimeoutWakeCount = transport.workerTimeoutWakeCount;
            result.audioTransportAppendRecentPcmCallCount = transport.appendRecentPcmCallCount;
            result.audioTransportAppendRecentPcmSamplesAppended = transport.appendRecentPcmSamplesAppended;
            result.audioTransportPrimedForDrain = transport.primedForDrain;
            result.audioTransportPrimedTransitionCount = transport.primedTransitionCount;
            result.audioTransportPrimingSilenceCallbackCount = transport.primingSilenceCallbackCount;
            result.audioTransportPrimingSilenceSamples = transport.primingSilenceSamples;
            result.audioTransportDrainDurationSampleCount = transport.drainCallbackDurationSampleCount;
            result.audioTransportDrainDurationLastNanos = transport.drainCallbackDurationLastNanos;
            result.audioTransportDrainDurationHighWaterNanos = transport.drainCallbackDurationHighWaterNanos;
            result.audioTransportDrainDurationP50Nanos = transport.drainCallbackDurationP50Nanos;
            result.audioTransportDrainDurationP95Nanos = transport.drainCallbackDurationP95Nanos;
            result.audioTransportDrainDurationP99Nanos = transport.drainCallbackDurationP99Nanos;
            result.audioTransportDrainDurationP999Nanos = transport.drainCallbackDurationP999Nanos;
            result.audioTransportDrainDurationUnder50usCount =
                transport.drainCallbackDurationUnder50usCount;
            result.audioTransportDrainDuration50To100usCount =
                transport.drainCallbackDuration50To100usCount;
            result.audioTransportDrainDuration100To250usCount =
                transport.drainCallbackDuration100To250usCount;
            result.audioTransportDrainDuration250To500usCount =
                transport.drainCallbackDuration250To500usCount;
            result.audioTransportDrainDuration500usTo1msCount =
                transport.drainCallbackDuration500usTo1msCount;
            result.audioTransportDrainDuration1To2msCount =
                transport.drainCallbackDuration1To2msCount;
            result.audioTransportDrainDuration2To5msCount =
                transport.drainCallbackDuration2To5msCount;
            result.audioTransportDrainDuration5To10msCount =
                transport.drainCallbackDuration5To10msCount;
            result.audioTransportDrainDurationOver10msCount =
                transport.drainCallbackDurationOver10msCount;
            result.audioTransportWorkerEmulationWakeLatencySampleCount = transport.workerEmulationWakeLatencySampleCount;
            result.audioTransportWorkerEmulationWakeLatencyLastNs = transport.workerEmulationWakeLatencyLastNs;
            result.audioTransportWorkerEmulationWakeLatencyHighWaterNs = transport.workerEmulationWakeLatencyHighWaterNs;
        }
    }

    std::shared_ptr<DynamicPluginModule::State> state_;
    const TimeFrontendApiV1* api_ = nullptr;
    void* instance_ = nullptr;
    std::string id_;
    std::string displayName_;
    SdlFrontendConfig config_;
    TimeFrontendHostApiV1 hostApi_{};
    std::unique_ptr<Presenter> presenter_;
    AudioService* audioService_ = nullptr;
    VideoService* videoService_ = nullptr;
    TimingService* timingService_ = nullptr;
    InputService* inputService_ = nullptr;
    DebugSnapshotService* debugSnapshotService_ = nullptr;
    std::unique_ptr<IAudioOutputBackend> audioOutput_;
    mutable SdlFrontendStats stats_{};
    std::atomic<std::size_t> videoEvents_{0u};
    std::atomic<std::size_t> framesPrepared_{0u};
    std::atomic<std::size_t> videoPacketsAccepted_{0u};
    std::atomic<std::size_t> videoPacketsSkipped_{0u};
    std::atomic<std::size_t> videoPacketsBuiltOutsideLock_{0u};
    std::atomic<std::int64_t> videoBuildLastNs_{0};
    std::atomic<std::int64_t> videoBuildHighWaterNs_{0};
    std::atomic<std::size_t> videoBuildSamples_{0u};
    std::atomic<std::size_t> audioEvents_{0u};
    std::atomic<std::size_t> audioPacketsAccepted_{0u};
    std::atomic<std::size_t> audioPacketsSkipped_{0u};
    std::atomic<std::size_t> audioBatchFlushCount_{0u};
    std::atomic<std::size_t> audioBatchFlushLast_{0u};
    std::atomic<std::size_t> audioBatchFlushMin_{0u};
    std::atomic<std::size_t> audioBatchFlushMax_{0u};
    std::atomic<std::size_t> audioBatchPacketsAccumulated_{0u};
    std::atomic<std::size_t> audioBatchPacketsFlushed_{0u};
    std::atomic<std::size_t> audioBatchCurrentSamples_{0u};
    std::atomic<std::size_t> inputEvents_{0u};
    std::atomic<std::size_t> inputSamplesProvided_{0u};
    mutable std::atomic<std::size_t> inputPolls_{0u};
    std::atomic<std::int32_t> inputMask_{0};
    std::atomic<bool> quitRequested_{false};
    std::atomic<std::size_t> quitRequestCount_{0u};
    bool visibilityRequested_ = false;
    std::vector<std::string> diagnostics_;
    std::optional<VideoDebugFrameModel> lastVideoDebugModel_;
    std::optional<AudioStateView> lastAudioState_;
    std::optional<SdlAudioPreviewBuffer> lastAudioPreview_;
    std::optional<DigitalInputStateView> lastInputState_;
    std::optional<SdlFrameBuffer> lastFrame_;
    std::string lastRenderSummary_;
    std::string lastHostEventSummary_;
    mutable std::string backendName_ = "frontend";
    mutable std::string lastBackendError_;
    std::vector<std::int16_t> audioBatchSamples_;
    std::size_t audioBatchPackets_ = 0u;
    std::uint32_t audioBatchSampleRate_ = 0u;
    std::uint8_t audioBatchChannels_ = 0u;
    std::uint64_t audioBatchFrameCounter_ = 0u;
};

[[nodiscard]] std::runtime_error loadError(const std::filesystem::path& path,
                                           const std::string& detail)
{
    return std::runtime_error("Unable to load plugin module '" + path.string() + "': " + detail);
}

} // namespace

DynamicPluginModule DynamicPluginModule::load(const std::filesystem::path& path)
{
    auto state = std::make_shared<State>();
    state->handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (state->handle == nullptr) {
        const char* error = dlerror();
        throw loadError(path, error != nullptr ? error : "unknown dynamic-loader error");
    }
    dlerror();
    const auto symbol = dlsym(state->handle, TIME_PLUGIN_MODULE_ENTRYPOINT_V1);
    if (const char* error = dlerror(); error != nullptr) throw loadError(path, error);
    const auto getModule = reinterpret_cast<TimeGetPluginModuleV1Fn>(symbol);
    if (getModule == nullptr) throw loadError(path, "module entrypoint is null");
    const auto* module = getModule();
    if (module == nullptr || module->struct_size < sizeof(TimePluginModuleV1) ||
        module->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || module->module_id == nullptr ||
        module->module_id[0] == '\0' || module->display_name == nullptr ||
        module->plugin_count > 1024u ||
        (module->plugin_count != 0u && module->plugin_at == nullptr)) {
        throw loadError(path, "invalid module descriptor");
    }
    state->moduleId = module->module_id;
    state->moduleDisplayName = module->display_name;
    std::unordered_set<std::string> ids;
    for (std::uint32_t index = 0; index < module->plugin_count; ++index) {
        const auto* descriptor = module->plugin_at(index);
        if (descriptor == nullptr || descriptor->struct_size < sizeof(TimePluginDescriptorV1) ||
            descriptor->plugin_id == nullptr || descriptor->plugin_id[0] == '\0' ||
            descriptor->display_name == nullptr || descriptor->api == nullptr) {
            throw loadError(path, "invalid plugin descriptor");
        }
        if (!ids.emplace(descriptor->plugin_id).second) {
            throw loadError(path, "duplicate plugin id: " + std::string(descriptor->plugin_id));
        }
        if (descriptor->kind == TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1) {
            if (descriptor->api_size < sizeof(TimeExecutorPolicyApiV1)) {
                throw loadError(path, "executor policy API size mismatch");
            }
            const auto* api = static_cast<const TimeExecutorPolicyApiV1*>(descriptor->api);
            if (api->struct_size < sizeof(TimeExecutorPolicyApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 || api->create == nullptr ||
                api->destroy == nullptr || api->backend == nullptr || api->guarantee == nullptr ||
                api->required_capabilities == nullptr || api->should_record == nullptr ||
                api->should_segment == nullptr) {
                throw loadError(path, "incomplete executor policy API");
            }
            state->executors.push_back(State::ExecutorEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else if (descriptor->kind == TIME_PLUGIN_KIND_FRONTEND_V1) {
            if (descriptor->api_size < sizeof(TimeFrontendApiV1)) {
                throw loadError(path, "frontend API size mismatch");
            }
            const auto* api = static_cast<const TimeFrontendApiV1*>(descriptor->api);
            constexpr std::uint32_t knownCapabilities = TIME_FRONTEND_CAPABILITY_VIDEO_V1 |
                TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1 | TIME_FRONTEND_CAPABILITY_WINDOW_V1;
            if (api->struct_size < sizeof(TimeFrontendApiV1) ||
                api->abi_version != TIME_PLUGIN_ABI_VERSION_V1 ||
                (api->capabilities & ~knownCapabilities) != 0u || api->create == nullptr ||
                api->destroy == nullptr || api->initialize == nullptr || api->shutdown == nullptr ||
                api->service == nullptr || api->present == nullptr ||
                api->set_window_visible == nullptr || api->backend_name == nullptr ||
                api->last_error == nullptr || api->query_stats == nullptr) {
                throw loadError(path, "incomplete frontend API");
            }
            state->frontends.push_back(State::FrontendEntry{
                descriptor->plugin_id, descriptor->display_name, api});
        } else {
            throw loadError(path, "unsupported plugin kind");
        }
    }
    return DynamicPluginModule(std::move(state));
}

std::string_view DynamicPluginModule::id() const noexcept
{
    return state_ != nullptr ? std::string_view(state_->moduleId) : std::string_view{};
}

std::string_view DynamicPluginModule::displayName() const noexcept
{
    return state_ != nullptr ? std::string_view(state_->moduleDisplayName) : std::string_view{};
}

std::vector<std::string> DynamicPluginModule::executorPolicyIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->executors.size());
    for (const auto& executor : state_->executors) result.push_back(executor.id);
    return result;
}

std::unique_ptr<IExecutorPolicyPlugin> DynamicPluginModule::createExecutorPolicy(
    std::string_view id) const
{
    if (state_ == nullptr) throw std::runtime_error("plugin module is empty");
    const auto found = std::find_if(state_->executors.begin(), state_->executors.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->executors.end()) {
        throw std::invalid_argument("executor policy not found in module: " + std::string(id));
    }
    void* instance = found->api->create(&CExecutorPolicyAdapter::hostApi());
    if (instance == nullptr) throw std::runtime_error("C executor policy factory returned null");
    std::unique_ptr<CExecutorPolicyAdapter> result;
    try {
        result = std::make_unique<CExecutorPolicyAdapter>(state_, *found, instance);
    } catch (...) {
        found->api->destroy(instance);
        throw;
    }
    validateExecutorPolicyStartup(*result);
    return result;
}

std::vector<std::string> DynamicPluginModule::frontendIds() const
{
    std::vector<std::string> result;
    if (state_ == nullptr) return result;
    result.reserve(state_->frontends.size());
    for (const auto& frontend : state_->frontends) result.push_back(frontend.id);
    return result;
}

std::unique_ptr<IFrontendPlugin> DynamicPluginModule::createFrontend(
    std::string_view id, const SdlFrontendConfig& config) const
{
    if (state_ == nullptr) throw std::runtime_error("plugin module is empty");
    const auto found = std::find_if(state_->frontends.begin(), state_->frontends.end(),
        [id](const auto& entry) { return entry.id == id; });
    if (found == state_->frontends.end()) {
        throw std::invalid_argument("frontend not found in module: " + std::string(id));
    }
    if (config.enableVideo &&
        (found->api->capabilities & TIME_FRONTEND_CAPABILITY_VIDEO_V1) == 0u) {
        throw std::invalid_argument("frontend does not provide video: " + std::string(id));
    }
    if (config.enableInput &&
        (found->api->capabilities & TIME_FRONTEND_CAPABILITY_DIGITAL_INPUT_V1) == 0u) {
        throw std::invalid_argument("frontend does not provide digital input: " + std::string(id));
    }
    return std::make_unique<CFrontendAdapter>(state_, *found, config);
}

} // namespace BMMQ::Plugin
