#ifndef TIME_FOREIGN_MACHINE_HPP
#define TIME_FOREIGN_MACHINE_HPP
#include "MachineFactory.hpp"
#include "machine/plugins/abi/TimeMachineRuntimeAbi.h"
namespace BMMQ {
class IForeignMachineRuntimeV2 {
public:
    virtual ~IForeignMachineRuntimeV2() = default;
};
// No core layouts cross this boundary. Ownership keeps all callbacks loaded
// through destruction; owned descriptors outlive the borrowed module tables.
std::unique_ptr<Machine> createForeignMachine(std::shared_ptr<const void> module,
    TimeMachineRuntimeProviderV2 provider, MachineDescriptor descriptor,
    std::vector<std::string> labels, std::vector<IoRegionDescriptor> regions);
}
#endif
