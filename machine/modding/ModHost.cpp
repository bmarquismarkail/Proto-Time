#include "ModHost.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace BMMQ::Modding {

ModHost::ModHost(const ModHost& other)
    : regions_(other.regions_), symbols_(other.symbols_), hooks_(other.hooks_), patches_(other.patches_) {}
ModHost::ModHost(ModHost&& other) { *this = std::move(other); }
ModHost& ModHost::operator=(const ModHost& other) {
    if (this != &other) { ModHost copy(other); *this = std::move(copy); }
    return *this;
}
ModHost& ModHost::operator=(ModHost&& other) noexcept {
    if (this != &other) {
        regions_ = std::move(other.regions_); symbols_ = std::move(other.symbols_);
        hooks_ = std::move(other.hooks_); patches_ = std::move(other.patches_);
    }
    return *this;
}


std::uint32_t ModHost::createRegion(std::string name, std::size_t size,
                                    std::uint16_t guestBase, std::uint8_t bank)
{
    if (name.empty() || size == 0u) {
        return 0u;
    }
    regions_.push_back(Region{std::move(name), std::vector<std::uint8_t>(size), guestBase, bank});
    return static_cast<std::uint32_t>(regions_.size());
}

std::span<std::uint8_t> ModHost::region(std::uint32_t id) noexcept
{
    if (id == 0u || id > regions_.size()) return {};
    return regions_[id - 1u].bytes;
}

std::span<const std::uint8_t> ModHost::region(std::uint32_t id) const noexcept
{
    if (id == 0u || id > regions_.size()) return {};
    return regions_[id - 1u].bytes;
}

const Region* ModHost::regionInfo(std::uint32_t id) const noexcept
{
    if (id == 0u || id > regions_.size()) return nullptr;
    return &regions_[id - 1u];
}

bool ModHost::mapRegion(std::uint32_t id, const WindowMapper& mapper) const
{
    const auto* info = regionInfo(id);
    return info != nullptr && static_cast<bool>(mapper) && mapper(info->guestBase, info->bytes, info->bank);
}

bool ModHost::addSymbol(std::string name, Symbol symbol)
{
    if (name.empty()) return false;
    return symbols_.emplace(std::move(name), symbol).second;
}

const Symbol* ModHost::resolveSymbol(std::string_view name) const
{
    const auto it = symbols_.find(std::string(name));
    return it == symbols_.end() ? nullptr : &it->second;
}

bool ModHost::applyPatch(const Symbol& target, std::span<const std::uint8_t> expected,
                         std::span<const std::uint8_t> replacement,
                         const PatchVerifier& verifier, const PatchWriter& writer)
{
    if (expected.empty() || expected.size() != replacement.size() || !verifier || !writer) return false;
    const PatchKey key{target.address, target.bank};
    const auto end = static_cast<std::uint32_t>(target.address) + replacement.size();
    if (end > 0x10000u) return false;
    for (const auto& [existing, size] : patches_) {
        if (existing.bank != target.bank) continue;
        const auto existingEnd = static_cast<std::uint32_t>(existing.address) + size;
        if (target.address < existingEnd && existing.address < end) return false;
    }
    if (!verifier(target.bank, target.address, expected)) return false;
    if (!writer(target.bank, target.address, replacement)) return false;
    patches_.emplace(key, replacement.size());
    return true;
}

std::uint32_t ModHost::registerHook(std::string symbolName, Hook hook)
{
    if (symbolName.empty() || !hook || hooks_.contains(symbolName)) return 0u;
    hooks_.emplace(std::move(symbolName), std::move(hook));
    return static_cast<std::uint32_t>(hooks_.size());
}

bool ModHost::invokeHook(std::string_view symbolName, HookContext& context) const
{
    const auto it = hooks_.find(std::string(symbolName));
    if (it == hooks_.end()) return false;
    it->second(context);
    return true;
}

void ModHost::reset() noexcept
{
    for (auto& region : regions_) std::fill(region.bytes.begin(), region.bytes.end(), 0u);
}

namespace {
constexpr std::size_t kStateBudget = 64u * 1024u * 1024u;
void append32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
}

std::vector<std::uint8_t> ModHost::exportState() const
{
    std::size_t size = 8;
    for (const auto& region : regions_) {
        if (region.name.size() > kStateBudget || region.bytes.size() > kStateBudget ||
            size > kStateBudget - 16 || region.name.size() > kStateBudget - size - 16 ||
            region.bytes.size() > kStateBudget - size - 16 - region.name.size())
            throw std::length_error("host-region state budget exhausted");
        size += 16 + region.name.size() + region.bytes.size();
    }
    std::vector<std::uint8_t> out;
    out.reserve(size);
    append32(out, 2u);
    append32(out, static_cast<std::uint32_t>(regions_.size()));
    for (const auto& region : regions_) {
        append32(out, static_cast<std::uint32_t>(region.name.size()));
        out.insert(out.end(), region.name.begin(), region.name.end());
        append32(out, region.guestBase);
        append32(out, region.bank);
        append32(out, static_cast<std::uint32_t>(region.bytes.size()));
        out.insert(out.end(), region.bytes.begin(), region.bytes.end());
    }
    return out;
}

ModHost::PreparedState ModHost::prepareState(std::span<const std::uint8_t> state) const
{
    if (state.size() > kStateBudget) throw std::length_error("host-region state budget exhausted");
    std::size_t pos = 0;
    const auto read32 = [&]() {
        if (state.size() - pos < 4) throw std::invalid_argument("truncated host-region state");
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(state[pos++]) << shift;
        return value;
    };
    const auto version = read32();
    if ((version != 1 && version != 2) || read32() != regions_.size())
        throw std::invalid_argument("incompatible host-region state");
    PreparedState prepared;
    prepared.bytes_.reserve(regions_.size());
    for (const auto& region : regions_) {
        if (version == 2) {
            const auto length = read32();
            if (length != region.name.size() || length > state.size() - pos ||
                !std::equal(region.name.begin(), region.name.end(), state.begin() + pos,
                    [](char name, std::uint8_t byte) { return static_cast<std::uint8_t>(name) == byte; }))
                throw std::invalid_argument("host-region identity mismatch");
            pos += length;
            if (read32() != region.guestBase || read32() != region.bank)
                throw std::invalid_argument("host-region mapping mismatch");
        }
        const auto size = read32();
        if (size != region.bytes.size() || size > state.size() - pos)
            throw std::invalid_argument("host-region size mismatch");
        prepared.bindings_.push_back({region.name, region.guestBase, region.bank});
        prepared.bytes_.emplace_back(state.begin() + pos, state.begin() + pos + size);
        pos += size;
    }
    if (pos != state.size()) throw std::invalid_argument("trailing host-region data");
    return prepared;
}

ModHost::PreparedState ModHost::prepareCheckpoint(std::optional<std::span<const std::uint8_t>> state) const
{
    if (!state) {
        if (hasRegions()) throw std::invalid_argument("checkpoint lacks required host-region state");
        return {};
    }
    // Machine checkpoints require identity-bound records. Schema 1 remains
    // readable through importState for existing standalone ModHost clients.
    if (state->size() < 4 || (*state)[0] != 2 || (*state)[1] || (*state)[2] || (*state)[3])
        throw std::invalid_argument("checkpoint host-region schema is incompatible");
    return prepareState(*state);
}

void ModHost::commitState(PreparedState&& state)
{
    // Reject stale layouts before writing any bytes.
    if (state.bytes_.size() != regions_.size() || state.bindings_.size() != regions_.size())
        throw std::invalid_argument("stale host-region preparation");
    for (std::size_t i = 0; i < regions_.size(); ++i)
        if (state.bytes_[i].size() != regions_[i].bytes.size() ||
            state.bindings_[i].name != regions_[i].name ||
            state.bindings_[i].base != regions_[i].guestBase || state.bindings_[i].bank != regions_[i].bank)
            throw std::invalid_argument("stale host-region preparation");
    for (std::size_t i = 0; i < regions_.size(); ++i)
        std::copy(state.bytes_[i].begin(), state.bytes_[i].end(), regions_[i].bytes.begin());
}

bool ModHost::importState(std::span<const std::uint8_t> state) noexcept
{
    try {
        auto prepared = prepareState(state);
        commitState(std::move(prepared));
        return true;
    } catch (...) { return false; }
}

} // namespace BMMQ::Modding
