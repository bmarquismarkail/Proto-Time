#include "DynamicPluginModule.hpp"

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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

    ~State() {
        if (handle != nullptr) dlclose(handle);
    }

    void* handle = nullptr;
    std::string moduleId;
    std::string moduleDisplayName;
    std::vector<ExecutorEntry> executors;
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
        if (descriptor->kind != TIME_PLUGIN_KIND_EXECUTOR_POLICY_V1) {
            throw loadError(path, "unsupported plugin kind");
        }
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

} // namespace BMMQ::Plugin
