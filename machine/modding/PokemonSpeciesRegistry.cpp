#include "PokemonSpeciesRegistry.hpp"
namespace BMMQ::Modding {
bool PokemonSpeciesRegistry::registerVanilla(std::uint8_t id, PokemonSpeciesRecord record)
{
    if (id == 0u || record.name.empty() || records_.contains(id)) return false;
    records_.emplace(id, std::move(record)); return true;
}
std::optional<PokemonSpeciesId> PokemonSpeciesRegistry::addExtended(PokemonSpeciesRecord record)
{
    if (record.name.empty() || extendedIds_.size() >= 0xFF00u) return std::nullopt;
    const auto id = static_cast<PokemonSpeciesId>(kPokemonExtendedIdBase + extendedIds_.size());
    if (!records_.emplace(id, std::move(record)).second) return std::nullopt;
    extendedIds_.push_back(id); return id;
}
const PokemonSpeciesRecord* PokemonSpeciesRegistry::resolve(PokemonSpeciesId id) const noexcept
{
    const auto it = records_.find(id); return it == records_.end() ? nullptr : &it->second;
}
std::optional<PokemonSpeciesId> PokemonSpeciesRegistry::select(std::uint16_t id) const noexcept
{
    return resolve(id) == nullptr ? std::nullopt : std::optional<PokemonSpeciesId>{id};
}
}
