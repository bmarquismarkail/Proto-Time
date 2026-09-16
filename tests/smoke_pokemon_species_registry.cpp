#include "machine/modding/PokemonSpeciesRegistry.hpp"
#include <cassert>
int main()
{
    using namespace BMMQ::Modding;
    PokemonSpeciesRegistry registry;
    assert(registry.registerVanilla(1u, {"RHYDON", {0x01u}}));
    assert(!registry.registerVanilla(1u, {"DUPLICATE", {}}));
    const auto first = registry.addExtended({"EXTENDED_A", {0xAAu}});
    const auto second = registry.addExtended({"EXTENDED_B", {0xBBu}});
    assert(first && *first == 0x0100u); assert(second && *second == 0x0101u);
    assert(registry.select(1u) && registry.select(1u).value() == 1u);
    assert(registry.select(0x0100u) && registry.resolve(0x0100u)->data[0] == 0xAAu);
    assert(!registry.select(0x00FFu)); assert(!registry.select(0x0200u));
    assert(registry.size() == 3u && registry.extendedSize() == 2u);
}
