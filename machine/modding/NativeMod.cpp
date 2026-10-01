#include "NativeMod.hpp"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace BMMQ::Modding {
struct NativeMod::Impl {
    void* library = nullptr;
    TimeModApiV1 api{};
    TimeModObserverV1 observer{};
    void* instance = nullptr;
    std::shared_ptr<ModHost> host;
    LoadedMod metadata;
    TimeModHostV1 bridge{};
    const std::thread::id owner = std::this_thread::get_id();
    bool active = false;
    bool allowed() const { return owner == std::this_thread::get_id() && active; }
    bool owns(std::uint32_t id) const {
        return std::any_of(metadata.regions.begin(), metadata.regions.end(),
                           [id](const auto& item) { return item.second == id; });
    }
    struct Call {
        Impl& state;
        explicit Call(Impl& s) : state(s) {
            if (s.owner != std::this_thread::get_id() || s.active)
                throw std::runtime_error("native mod thread/reentrancy violation");
            s.active = true;
        }
        ~Call() { state.active = false; }
    };
    ~Impl() {
        if (instance && api.destroy) {
            active = true;
            try { api.destroy(instance); } catch (...) {}
        }
        if (library) dlclose(library);
    }
    static int32_t find(void* context, const char* name, uint32_t* out) noexcept {
        try {
            auto& s = *static_cast<Impl*>(context);
            if (!s.allowed() || !name || !out) return 0;
            const auto it = s.metadata.regions.find(name);
            if (it == s.metadata.regions.end()) return 0;
            *out = it->second;
            return 1;
        } catch (...) { return 0; }
    }
    template<bool Write>
    static int32_t transfer(void* context, uint32_t id, uint64_t offset,
                           std::conditional_t<Write, const uint8_t*, uint8_t*> data, uint32_t size) noexcept {
        try {
            auto& s = *static_cast<Impl*>(context);
            if (!s.allowed() || !s.owns(id) || (!data && size)) return 0;
            auto bytes = s.host->region(id);
            if (bytes.empty() || offset > bytes.size() || size > bytes.size() - offset) return 0;
            if (size) {
                if constexpr (Write) std::memcpy(bytes.data() + offset, data, size);
                else std::memcpy(data, bytes.data() + offset, size);
            }
            return 1;
        } catch (...) { return 0; }
    }
    static int32_t symbol(void* context, const char* name, uint32_t* bank, uint32_t* address) noexcept {
        try {
            auto& s = *static_cast<Impl*>(context);
            if (!s.allowed() || !name || !bank || !address) return 0;
            const auto* value = s.host->resolveSymbol(name);
            if (!value) return 0;
            *bank = value->bank; *address = value->address;
            return 1;
        } catch (...) { return 0; }
    }
};
NativeMod::NativeMod() : impl_(std::make_unique<Impl>()) {}
NativeMod::~NativeMod() = default;

std::unique_ptr<NativeMod> NativeMod::load(const LoadedMod& metadata, std::shared_ptr<ModHost> host)
{
    if (!host || metadata.nativeModule.empty()) throw std::runtime_error("missing native module or host");
    auto mod = std::unique_ptr<NativeMod>(new NativeMod);
    auto& s = *mod->impl_;
    s.host = std::move(host);
    s.metadata = metadata;
    for (const auto& [name, id] : metadata.regions)
        if (name.empty() || !s.host->regionInfo(id)) throw std::runtime_error("invalid module region binding");
    s.library = dlopen(metadata.nativeModule.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!s.library) throw std::runtime_error(dlerror());
    auto entry = reinterpret_cast<TimeGetModV1>(dlsym(s.library, TIME_MOD_ENTRYPOINT_V1));
    if (!entry) throw std::runtime_error("missing time_get_mod_v1");
    const auto* api = entry();
    if (!api || api->struct_size < sizeof(TimeModApiV1) || api->abi_version != TIME_MOD_ABI_V1)
        throw std::runtime_error("incompatible native mod ABI");
    if (!api->id || !api->version || metadata.id != api->id || metadata.version != api->version)
        throw std::runtime_error("native mod identity mismatch");
    if (!api->create || !api->destroy || !api->invoke || !api->reset || !api->save || !api->restore ||
        api->state_size > 16 * 1024 * 1024)
        throw std::runtime_error("invalid native mod callbacks/state size");
    if (auto entry = reinterpret_cast<TimeGetModObserverV1>(dlsym(s.library, TIME_MOD_OBSERVER_ENTRYPOINT_V1))) {
        const auto* extension = entry();
        if (!extension || extension->struct_size < sizeof(TimeModObserverV1) ||
            extension->abi_version != 1 || !extension->observe)
            throw std::runtime_error("incompatible native observer ABI");
        s.observer = *extension;
    }
    s.api = *api;
    s.bridge = {sizeof(TimeModHostV1), TIME_MOD_ABI_V1, &s,
                Impl::find, Impl::transfer<false>, Impl::transfer<true>, Impl::symbol};
    Impl::Call call(s);
    if (s.api.create(&s.bridge, &s.instance) != 1 || !s.instance)
        throw std::runtime_error("native mod create failed");
    return mod;
}
std::unique_ptr<NativeMod> NativeMod::load(const LoadedMod& metadata, ModHost& host)
{
    return load(metadata, std::shared_ptr<ModHost>(&host, [](ModHost*) {}));
}
bool NativeMod::invoke(TimeModCallV1& input)
{
    auto& s = *impl_;
    Impl::Call guard(s);
    if (input.struct_size < sizeof(TimeModCallV1)) return false;
    auto copy = input;
    if (s.api.invoke(s.instance, &copy) != 1) return false;
    input.result = copy.result;
    return true;
}
bool NativeMod::observe(const TimeModObservationV1& observation) {
    Impl::Call guard(*impl_);
    if (!impl_->observer.observe) return true;
    if (observation.struct_size < sizeof(TimeModObservationV1) || observation.abi_version != 1) return false;
    return impl_->observer.observe(impl_->instance, &observation) == 1;
}
bool NativeMod::reset() {
    Impl::Call guard(*impl_);
    return impl_->api.reset(impl_->instance) == 1;
}
NativeModState NativeMod::save() {
    auto& s = *impl_;
    Impl::Call guard(s);
    NativeModState state{s.metadata.id, s.metadata.version, s.api.state_version,
                         std::vector<std::uint8_t>(s.api.state_size)};
    if (s.api.save(s.instance, state.bytes.data(), state.bytes.size()) != 1)
        throw std::runtime_error("native mod save failed");
    return state;
}
bool NativeMod::restore(const NativeModState& state) {
    auto& s = *impl_;
    Impl::Call guard(s);
    if (state.id != s.metadata.id || state.version != s.metadata.version ||
        state.schema != s.api.state_version || state.bytes.size() != s.api.state_size) return false;
    return s.api.restore(s.instance, state.bytes.data(), state.bytes.size()) == 1;
}
}
