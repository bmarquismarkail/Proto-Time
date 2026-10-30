#include "NativeMod.hpp"
#include "../Machine.hpp"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <fstream>
#include <map>
#include <mutex>
#include <array>
#include <openssl/sha.h>

namespace BMMQ::Modding {
namespace {
std::mutex cohortMutex;
std::map<const ModHost*, std::vector<NativeMod*>> cohorts;
std::vector<NativeMod*> cohortMembers(const ModHost& host) {
    std::lock_guard lock(cohortMutex);
    const auto it = cohorts.find(&host);
    auto members = it == cohorts.end() ? std::vector<NativeMod*>{} : it->second;
    std::erase_if(members, [&host](auto* mod) { return !mod->attachedTo(host); });
    return members;
}
using Digest = std::array<unsigned char, SHA256_DIGEST_LENGTH>;
Digest imageDigest(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot bind native module image");
    std::vector<unsigned char> bytes;
    std::array<char, 8192> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        if (bytes.size() + input.gcount() > 128u * 1024u * 1024u)
            throw std::length_error("native image budget exhausted");
        bytes.insert(bytes.end(), buffer.data(), buffer.data() + input.gcount());
    }
    if (!input.eof()) throw std::runtime_error("cannot read native module image");
    Digest out;
    SHA256(bytes.data(), bytes.size(), out.data());
    return out;
}
void append32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(value >> (8 * i));
}
void appendText(std::vector<std::uint8_t>& out, const std::string& value) {
    if (value.size() > 65536) throw std::length_error("native identity budget exhausted");
    append32(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}
}

struct NativeMod::Impl {
    void* library = nullptr;
    TimeModApiV1 api{};
    TimeModObserverV1 observer{};
    void* instance = nullptr;
    std::shared_ptr<ModHost> host;
    std::weak_ptr<const void> hostLifetime;
    LoadedMod metadata;
    TimeModHostV1 bridge{};
    const std::thread::id owner = std::this_thread::get_id();
    bool active = false;
    bool registered = false;
    bool writable = true;
    Digest image{};
    std::string executionBinding;
    Digest identity() const {
        std::vector<std::uint8_t> bytes(image.begin(), image.end());
        appendText(bytes, metadata.id); appendText(bytes, metadata.version);
        append32(bytes, metadata.priority);
        std::map<std::string, std::uint32_t> regions(metadata.regions.begin(), metadata.regions.end());
        append32(bytes, regions.size());
        for (const auto& [name, id] : regions) { appendText(bytes, name); append32(bytes, id); }
        append32(bytes, metadata.trampolines.size());
        for (const auto& hook : metadata.trampolines) { appendText(bytes, hook.symbol); append32(bytes, hook.hookId); }
        appendText(bytes, executionBinding);
        Digest result;
        SHA256(bytes.data(), bytes.size(), result.data());
        return result;
    }
    bool allowed() const { return owner == std::this_thread::get_id() && active && !hostLifetime.expired(); }
    bool owns(std::uint32_t id) const {
        return std::any_of(metadata.regions.begin(), metadata.regions.end(),
                           [id](const auto& item) { return item.second == id; });
    }
    struct Call {
        Impl& state;
        explicit Call(Impl& s) : state(s) {
            if (s.owner != std::this_thread::get_id() || s.active)
                throw std::runtime_error("native mod thread/reentrancy violation");
            if (s.hostLifetime.expired()) throw std::runtime_error("native mod host expired");
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
            if constexpr (Write) { if (!s.writable) return 0; }
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
NativeMod::~NativeMod() {
    if (impl_->registered) {
        std::lock_guard lock(cohortMutex);
        auto it = cohorts.find(impl_->host.get());
        if (it != cohorts.end()) {
            std::erase(it->second, this);
            if (it->second.empty()) cohorts.erase(it);
        }
    }
}

std::unique_ptr<NativeMod> NativeMod::load(const LoadedMod& metadata, std::shared_ptr<ModHost> host)
{
    return loadImpl(metadata, std::move(host), true);
}
std::unique_ptr<NativeMod> NativeMod::loadImpl(const LoadedMod& metadata, std::shared_ptr<ModHost> host, bool registerInstance)
{
    if (!host || metadata.nativeModule.empty()) throw std::runtime_error("missing native module or host");
    auto mod = std::unique_ptr<NativeMod>(new NativeMod);
    auto& s = *mod->impl_;
    s.host = std::move(host);
    s.hostLifetime = s.host->lifetime();
    s.metadata = metadata;
    for (const auto& [name, id] : metadata.regions)
        if (name.empty() || !s.host->regionInfo(id)) throw std::runtime_error("invalid module region binding");
    s.image = imageDigest(metadata.nativeModule);
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
    if (registerInstance) {
        std::lock_guard lock(cohortMutex);
        cohorts[s.host.get()].push_back(mod.get());
        s.registered = true;
    }
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
    struct ReadOnly { Impl& s; ReadOnly(Impl& value) : s(value) { s.writable = false; }
        ~ReadOnly() { s.writable = true; } } readOnly(s);
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

bool NativeMod::attachedTo(const ModHost& host) const noexcept { return !impl_->hostLifetime.expired() && impl_->host.get() == &host; }
void NativeMod::setExecutionBinding(std::string binding) {
    Impl::Call guard(*impl_);
    if (!impl_->executionBinding.empty()) throw std::invalid_argument("native module already bound");
    impl_->executionBinding = std::move(binding);
}
struct NativeMod::PreparedCohort::State {
    ModHost* host = nullptr;
    std::weak_ptr<const void> hostLifetime;
    std::shared_ptr<ModHost> staged, retired;
    ModHost::PreparedState bytes;
    std::vector<std::uint8_t> beforeHost, beforeModules;
    std::vector<NativeMod*> live;
    std::vector<std::unique_ptr<NativeMod>> replacements;
};
NativeMod::PreparedCohort::PreparedCohort(std::unique_ptr<State> state) : state_(std::move(state)) {}
NativeMod::PreparedCohort::~PreparedCohort() = default;
NativeMod::PreparedCohort::PreparedCohort(PreparedCohort&&) noexcept = default;
NativeMod::PreparedCohort& NativeMod::PreparedCohort::operator=(PreparedCohort&&) noexcept = default;
void NativeMod::PreparedCohort::commit() {
    if (!state_) return;
    auto& s = *state_;
    if (s.hostLifetime.expired()) throw std::invalid_argument("expired native cohort host");
    if (cohortMembers(*s.host) != s.live || s.host->exportState() != s.beforeHost ||
        NativeMod::saveCohort(*s.host) != s.beforeModules)
        throw std::invalid_argument("stale native cohort preparation");
    s.host->commitState(std::move(s.bytes));
    for (std::size_t i = 0; i < s.live.size(); ++i) {
        // Destroy the retired instance against its own host, never published bytes.
        auto& old = s.live[i]->impl_;
        auto& next = s.replacements[i]->impl_;
        next->host = old->host;
        next->hostLifetime = old->hostLifetime;
        old->host = s.retired;
        old->hostLifetime = s.retired->lifetime();
        next->registered = old->registered;
        old->registered = false;
        old.swap(next);
    }
    state_.reset();
}
bool NativeMod::hasCohort(const ModHost& host) { return !cohortMembers(host).empty(); }
std::vector<std::uint8_t> NativeMod::saveCohort(const ModHost& host) {
    std::vector<std::uint8_t> out;
    append32(out, 1);
    const auto members = cohortMembers(host);
    const std::size_t count = members.size();
    if (count > 256) throw std::length_error("native cohort budget exhausted");
    append32(out, count);
    for (auto* mod : members) {
        const auto state = mod->save();
        if (out.size() + state.bytes.size() > 64u * 1024u * 1024u - 40)
            throw std::length_error("native cohort state budget exhausted");
        const auto identity = mod->impl_->identity();
        out.insert(out.end(), identity.begin(), identity.end());
        append32(out, state.schema); append32(out, state.bytes.size());
        out.insert(out.end(), state.bytes.begin(), state.bytes.end());
    }
    return out;
}
NativeMod::PreparedCohort NativeMod::prepareCohort(ModHost& host,
    std::optional<std::span<const std::uint8_t>> modules,
    std::optional<std::span<const std::uint8_t>> regions) {
    auto s = std::make_unique<PreparedCohort::State>();
    s->host = &host;
    s->hostLifetime = host.lifetime();
    s->live = cohortMembers(host);
    s->beforeHost = host.exportState();
    s->beforeModules = saveCohort(host);
    s->bytes = host.prepareCheckpoint(regions);
    if (!modules && !s->live.empty()) throw std::invalid_argument("checkpoint lacks native cohort");
    const std::vector<std::uint8_t> empty{1,0,0,0,0,0,0,0};
    const auto data = modules.value_or(std::span<const std::uint8_t>(empty));
    if (data.size() > 64u * 1024u * 1024u) throw std::length_error("native cohort budget exhausted");
    std::size_t pos = 0;
    auto read32 = [&]() {
        if (data.size() - pos < 4) throw std::invalid_argument("truncated native cohort");
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(data[pos++]) << (8*i);
        return value;
    };
    if (read32() != 1 || read32() != s->live.size()) throw std::invalid_argument("native cohort mismatch");
    // Validate every identity and payload before running any replacement callback.
    std::vector<NativeModState> states;
    for (auto* live : s->live) {
        Impl::Call guard(*live->impl_);
        const auto identity = live->impl_->identity();
        if (data.size() - pos < identity.size() ||
            !std::equal(identity.begin(), identity.end(), data.begin() + pos))
            throw std::invalid_argument("native checkpoint identity mismatch");
        pos += identity.size();
        const auto schema = read32(), size = read32();
        if (schema != live->impl_->api.state_version || size != live->impl_->api.state_size || size > data.size() - pos)
            throw std::invalid_argument("native checkpoint schema mismatch");
        states.push_back({live->impl_->metadata.id, live->impl_->metadata.version, schema,
            std::vector<std::uint8_t>(data.begin() + pos, data.begin() + pos + size)});
        pos += size;
        if (imageDigest(live->impl_->metadata.nativeModule) != live->impl_->image)
            throw std::invalid_argument("native module image changed since loading");
    }
    if (pos != data.size()) throw std::invalid_argument("trailing native cohort data");
    s->staged = std::make_shared<ModHost>(host);
    s->retired = std::make_shared<ModHost>(host);
    s->staged->commitState(s->staged->prepareCheckpoint(regions));
    const auto expectedHost = s->staged->exportState();
    for (std::size_t i = 0; i < s->live.size(); ++i) {
        auto* live = s->live[i];
        auto candidate = loadImpl(live->impl_->metadata, s->staged, false);
        candidate->impl_->executionBinding = live->impl_->executionBinding;
        if (!candidate->restore(states[i]) || candidate->save().bytes != states[i].bytes)
            throw std::invalid_argument("native checkpoint restore rejected");
        s->replacements.push_back(std::move(candidate));
    }
    if (s->staged->exportState() != expectedHost)
        throw std::invalid_argument("native restore disagrees with saved host-region state");
    return PreparedCohort(std::move(s));
}
NativeMod::PreparedCohort NativeMod::prepareReset(ModHost& host) {
    const auto modules = saveCohort(host);
    const auto regions = host.exportState();
    auto prepared = prepareCohort(host, modules, regions);
    auto& s = *prepared.state_;
    s.staged->reset();
    for (auto& replacement : s.replacements)
        if (!replacement->reset()) throw std::runtime_error("native reset rejected");
    s.bytes = host.prepareCheckpoint(s.staged->exportState());
    return prepared;
}
}

namespace BMMQ {
void Machine::resetModState() {
    auto prepared = Modding::NativeMod::prepareReset(modHost());
    prepared.commit();
    advanceObservationGeneration();
}
}
