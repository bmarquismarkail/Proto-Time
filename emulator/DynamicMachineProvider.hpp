#ifndef TIME_DYNAMIC_MACHINE_PROVIDER_HPP
#define TIME_DYNAMIC_MACHINE_PROVIDER_HPP
#include <filesystem>
#include "MachineFactory.hpp"
namespace BMMQ {
// Registration validates the entire module before publishing any provider.
// Registration and creation are control/machine-lane operations.
void registerDynamicMachineProviders(MachineRegistry&, const std::filesystem::path&);
MachineInstance createProvidedMachine(std::string_view id, const std::optional<std::filesystem::path>& module);
}
#endif
