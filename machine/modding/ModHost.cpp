#include "ModHost.hpp"

#include <algorithm>
#include <limits>

namespace BMMQ::Modding {

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

std::vector<std::uint8_t> ModHost::exportState() const
{
    std::vector<std::uint8_t> out;
    const auto append32 = [&out](std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value));
        out.push_back(static_cast<std::uint8_t>(value >> 8u));
        out.push_back(static_cast<std::uint8_t>(value >> 16u));
        out.push_back(static_cast<std::uint8_t>(value >> 24u));
    };
    append32(1u);
    append32(static_cast<std::uint32_t>(regions_.size()));
    for (const auto& region : regions_) {
        append32(static_cast<std::uint32_t>(region.bytes.size()));
        out.insert(out.end(), region.bytes.begin(), region.bytes.end());
    }
    return out;
}

bool ModHost::importState(std::span<const std::uint8_t> state) noexcept
{
    if (state.size() < 8u) return false;
    std::size_t pos = 0;
    const auto read32 = [&state, &pos]() -> std::uint32_t {
        if (pos + 4u > state.size()) return std::numeric_limits<std::uint32_t>::max();
        const auto value = static_cast<std::uint32_t>(state[pos]) |
            (static_cast<std::uint32_t>(state[pos + 1u]) << 8u) |
            (static_cast<std::uint32_t>(state[pos + 2u]) << 16u) |
            (static_cast<std::uint32_t>(state[pos + 3u]) << 24u);
        pos += 4u;
        return value;
    };
    if (read32() != 1u || read32() != regions_.size()) return false;
    std::vector<std::vector<std::uint8_t>> restored;
    restored.reserve(regions_.size());
    for (const auto& region : regions_) {
        const auto size = read32();
        if (size != region.bytes.size() || pos + size > state.size()) return false;
        restored.emplace_back(state.begin() + static_cast<std::ptrdiff_t>(pos),
                              state.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
    }
    if (pos != state.size()) return false;
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        std::copy(restored[i].begin(), restored[i].end(), regions_[i].bytes.begin());
    }
    return true;
}

} // namespace BMMQ::Modding
