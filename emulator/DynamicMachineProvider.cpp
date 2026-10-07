#include "DynamicMachineProvider.hpp"
#include "machine/plugins/abi/TimeMachineProviderAbi.h"
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace BMMQ {
namespace {
struct Module {
    void* handle = nullptr;
    ~Module() { if (handle) dlclose(handle); }
};
std::string bounded(const char* value, std::size_t max, bool identifier = true) {
    if (!value) throw std::invalid_argument("null machine provider string");
    const auto size = strnlen(value, max + 1);
    if (!size || size > max) throw std::invalid_argument("machine provider string budget exceeded");
    std::string result(value, size);
    if (!identifier && !std::all_of(result.begin(), result.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
        throw std::invalid_argument("invalid machine provider display name");
    if (identifier && !std::all_of(result.begin(), result.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
        })) throw std::invalid_argument("invalid machine provider identifier");
    return result;
}
struct Construction {
    std::string family;
    std::uint64_t ticket;
    std::unique_ptr<Machine> machine;
    bool failed = false;
};
std::atomic<std::uint64_t> nextTicket{1};
int32_t createFamily(void* opaque, const char* family, uint64_t* handle) noexcept {
    auto& c = *static_cast<Construction*>(opaque);
    try {
        if (!handle || c.machine || bounded(family, 64) != c.family) throw std::invalid_argument("factory family mismatch");
        c.machine = MachineRegistry::builtins().create(c.family);
        *handle = c.ticket;
        return 1;
    } catch (...) { c.failed = true; return 0; }
}
int32_t loadBios(void* opaque, uint64_t handle, const uint8_t* bytes, uint32_t size) noexcept {
    auto& c = *static_cast<Construction*>(opaque);
    try {
        if (!c.machine || handle != c.ticket || !bytes || !size || size > 32768)
            throw std::invalid_argument("invalid factory BIOS binding");
        auto* boot = dynamic_cast<IExternalBootRomMachine*>(c.machine.get());
        if (!boot) throw std::invalid_argument("BIOS capability unavailable");
        boot->loadExternalBootRom(std::vector<std::uint8_t>(bytes, bytes + size));
        c.machine->setProviderBootRom(std::vector<std::uint8_t>(bytes, bytes + size));
        return 1;
    } catch (...) { c.failed = true; return 0; }
}
struct Instance {
    std::shared_ptr<Module> module;
    void* opaque = nullptr;
    void (*release)(void*) = nullptr;
    ~Instance() {
        // The ABI requires nonthrowing release. Keep destruction nonthrowing even
        // for a C++ module violating that contract; it still retains its library.
        if (release) { try { release(opaque); } catch (...) {} }
    }
};
std::unique_ptr<Machine> construct(std::shared_ptr<Module> module, TimeMachineProviderV1 provider,
                                 const std::string& family) {
    auto ticket = nextTicket.load();
    do {
        if (!ticket || ticket == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("machine provider handle generation exhausted");
    } while (!nextTicket.compare_exchange_weak(ticket, ticket + 1));
    Construction c{family, ticket, {}, false};
    const TimeMachineFactoryHostV1 host{sizeof(host), TIME_MACHINE_PROVIDER_ABI_V1, &c, createFamily, loadBios};
    TimeMachineFactoryResultV1 result{sizeof(result), 0, nullptr, nullptr};
    // Adopt release before checking the result, including a factory that declines
    // after allocating metadata. Machine destruction always precedes its release.
    auto instance = std::make_shared<Instance>();
    instance->module = std::move(module);
    int32_t accepted = 0;
    try { accepted = provider.create(&host, &result); }
    catch (...) {
        instance->opaque = result.module_instance; instance->release = result.release;
        c.machine.reset();
        throw std::runtime_error("machine provider factory threw across C boundary");
    }
    instance->opaque = result.module_instance; instance->release = result.release;
    if (accepted != 1 || result.struct_size != sizeof(result) || !result.release ||
        c.failed || !c.machine || result.handle != ticket || c.machine->visualTargetId() != family) {
        c.machine.reset();
        throw std::runtime_error("machine provider factory result rejected");
    }
    try { c.machine->retainProviderLifetime(instance); }
    catch (...) { c.machine.reset(); throw; }
    return std::move(c.machine);
}
}
void registerDynamicMachineProviders(MachineRegistry& registry, const std::filesystem::path& path) {
    auto module = std::make_shared<Module>();
    module->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module->handle) throw std::runtime_error("cannot load machine provider module: " + std::string(dlerror()));
    dlerror();
    auto entry = reinterpret_cast<TimeGetMachineProviderModuleV1>(dlsym(module->handle, TIME_MACHINE_PROVIDER_ENTRY_V1));
    if (const auto* error = dlerror()) throw std::runtime_error("machine provider entry missing: " + std::string(error));
    const TimeMachineProviderModuleV1* descriptor = nullptr;
    try { descriptor = entry(); } catch (...) { throw std::runtime_error("machine provider entry threw across C boundary"); }
    if (!descriptor || descriptor->struct_size != sizeof(*descriptor) ||
        descriptor->abi_version != TIME_MACHINE_PROVIDER_ABI_V1 || !descriptor->provider_count ||
        descriptor->provider_count > 64 || !descriptor->providers)
        throw std::invalid_argument("machine provider module ABI rejected");
    (void)bounded(descriptor->id, 64);
    std::vector<std::pair<MachineDescriptor, MachineRegistry::Factory>> staged;
    for (std::uint32_t i = 0; i < descriptor->provider_count; ++i) {
        const auto p = descriptor->providers[i];
        if (p.struct_size != sizeof(p) || p.abi_version != TIME_MACHINE_PROVIDER_ABI_V1 ||
            !p.create || p.frame_width != 160 || p.frame_height != 144)
            throw std::invalid_argument("machine provider descriptor ABI rejected");
        MachineDescriptor machine{bounded(p.id, 64), bounded(p.display_name, 128, false),
            static_cast<int>(p.frame_width), static_cast<int>(p.frame_height), bounded(p.family, 64)};
        // Version 1 constructs the two current hardware implementations; it does
        // not claim an independently authored CPU/device table is supported.
        const auto family = machine.familyId;
        staged.emplace_back(std::move(machine), [module, p, family] { return construct(module, p, family); });
    }
    registry.registerProviders(std::move(staged));
}
MachineInstance createProvidedMachine(std::string_view id, const std::optional<std::filesystem::path>& module) {
    if (!module) return createMachine(id);
    MachineRegistry registry;
    registerDynamicMachineProviders(registry, *module);
    const auto descriptor = registry.descriptor(id);
    return {parseMachineKind(descriptor.familyId), descriptor, registry.create(id)};
}
}
