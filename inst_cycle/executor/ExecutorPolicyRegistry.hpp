#ifndef BMMQ_EXECUTOR_POLICY_REGISTRY_HPP
#define BMMQ_EXECUTOR_POLICY_REGISTRY_HPP

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "PluginContract.hpp"

namespace BMMQ::Plugin {

class ExecutorPolicyRegistry {
public:
    using Factory = std::function<std::unique_ptr<IExecutorPolicyPlugin>()>;

    void registerFactory(std::string id, Factory factory);
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    [[nodiscard]] std::unique_ptr<IExecutorPolicyPlugin> create(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

    [[nodiscard]] static const ExecutorPolicyRegistry& builtins();

private:
    struct Entry {
        std::string id;
        Factory factory;
    };
    std::vector<Entry> entries_;
};

[[nodiscard]] std::string_view executorPolicyIdForLegacyMode(std::string_view mode);

} // namespace BMMQ::Plugin

#endif // BMMQ_EXECUTOR_POLICY_REGISTRY_HPP
