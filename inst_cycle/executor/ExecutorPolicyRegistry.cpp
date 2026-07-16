#include "ExecutorPolicyRegistry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace BMMQ::Plugin {

void ExecutorPolicyRegistry::registerFactory(std::string id, Factory factory)
{
    if (id.empty()) throw std::invalid_argument("executor policy id is empty");
    if (!factory) throw std::invalid_argument("executor policy factory is empty");
    if (contains(id)) throw std::invalid_argument("duplicate executor policy id: " + id);
    entries_.push_back(Entry{std::move(id), std::move(factory)});
}

bool ExecutorPolicyRegistry::contains(std::string_view id) const noexcept
{
    return std::any_of(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry.id == id;
    });
}

std::unique_ptr<IExecutorPolicyPlugin> ExecutorPolicyRegistry::create(std::string_view id) const
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry.id == id;
    });
    if (found == entries_.end()) {
        throw std::invalid_argument("unknown executor policy id: " + std::string(id));
    }
    auto policy = found->factory();
    if (!policy) throw std::runtime_error("executor policy factory returned null: " + found->id);
    validateExecutorPolicyStartup(*policy);
    if (policy->metadata().id != found->id) {
        throw std::runtime_error("executor policy factory metadata id mismatch");
    }
    return policy;
}

std::vector<std::string> ExecutorPolicyRegistry::ids() const
{
    std::vector<std::string> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(entry.id);
    return result;
}

const ExecutorPolicyRegistry& ExecutorPolicyRegistry::builtins()
{
    static const ExecutorPolicyRegistry registry = [] {
        ExecutorPolicyRegistry result;
        result.registerFactory("bmmq.executor.policy.default-step", [] {
            return std::make_unique<DefaultStepPolicy>();
        });
        result.registerFactory("bmmq.executor.policy.visible-state-preserving-step", [] {
            return std::make_unique<VisibleStatePreservingStepPolicy>();
        });
        result.registerFactory("bmmq.executor.policy.portable-ir", [] {
            return std::make_unique<PortableIrStepPolicy>();
        });
        result.registerFactory("bmmq.executor.policy.native-experimental", [] {
            return std::make_unique<NativeExperimentalStepPolicy>();
        });
        return result;
    }();
    return registry;
}

std::string_view executorPolicyIdForLegacyMode(std::string_view mode)
{
    if (mode == "baseline") return "bmmq.executor.policy.default-step";
    if (mode == "block") return "bmmq.executor.policy.visible-state-preserving-step";
    if (mode == "ir") return "bmmq.executor.policy.portable-ir";
    if (mode == "native") return "bmmq.executor.policy.native-experimental";
    throw std::invalid_argument("unknown legacy CPU mode: " + std::string(mode));
}

} // namespace BMMQ::Plugin
