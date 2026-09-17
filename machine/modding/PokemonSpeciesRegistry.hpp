#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace BMMQ::Modding {
using PokemonSpeciesId = std::uint16_t;
constexpr PokemonSpeciesId kPokemonVanillaIdMax = 0x00FFu;
constexpr PokemonSpeciesId kPokemonExtendedIdBase = 0x0100u;
struct PokemonSpeciesRecord { std::string name; std::vector<std::uint8_t> data; };
class PokemonSpeciesRegistry final {
public:
    [[nodiscard]] bool registerVanilla(std::uint8_t id, PokemonSpeciesRecord record);
    [[nodiscard]] std::optional<PokemonSpeciesId> addExtended(PokemonSpeciesRecord record);
    [[nodiscard]] const PokemonSpeciesRecord* resolve(PokemonSpeciesId id) const noexcept;
    [[nodiscard]] std::optional<PokemonSpeciesId> select(std::uint16_t id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] std::size_t extendedSize() const noexcept { return extendedIds_.size(); }
private:
    std::unordered_map<PokemonSpeciesId, PokemonSpeciesRecord> records_;
    std::vector<PokemonSpeciesId> extendedIds_;
};
}
