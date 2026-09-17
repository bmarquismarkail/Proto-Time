#include "machine/modding/ModHost.hpp"
#include "machine/modding/PokeredSymbols.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

int main()
{
    BMMQ::Modding::ModHost host;
    const auto regionId = host.createRegion("species", 4u, 0x4000u, 2u);
    assert(regionId != 0u);
    auto bytes = host.region(regionId);
    bytes[0] = 0x2Au;
    assert(host.regionInfo(regionId)->guestBase == 0x4000u);

    const auto symbolPath = std::filesystem::temp_directory_path() / "proto_time_pokered_test.sym";
    {
        std::ofstream output(symbolPath);
        output << "00:0150 _Start\n";
        output << "02:4000 SpeciesData\n";
        output << "bad line\n";
    }
    const auto symbols = BMMQ::Modding::loadPokeredSymbols(symbolPath, host);
    std::filesystem::remove(symbolPath);
    assert(symbols.loaded == 2u);
    assert(host.resolveSymbol("_Start")->address == 0x0150u);
    assert(host.resolveSymbol("SpeciesData")->bank == 2u);

    const auto target = *host.resolveSymbol("_Start");
    const std::vector<std::uint8_t> expected{0x01u, 0x02u};
    const std::vector<std::uint8_t> replacement{0x03u, 0x04u};
    bool wrote = false;
    assert(host.applyPatch(target, expected, replacement,
                           [](std::uint8_t, std::uint16_t, std::span<const std::uint8_t> value) {
                               return value[0] == 0x01u && value[1] == 0x02u;
                           },
                           [&wrote, &replacement](std::uint8_t, std::uint16_t,
                                                   std::span<const std::uint8_t> value) {
                               wrote = value[0] == replacement[0] && value[1] == replacement[1];
                               return wrote;
                           }));
    assert(wrote);
    assert(!host.applyPatch(target, expected, replacement,
                            [](std::uint8_t, std::uint16_t, std::span<const std::uint8_t>) { return true; },
                            [](std::uint8_t, std::uint16_t, std::span<const std::uint8_t>) { return true; }));

    assert(host.registerHook("SpeciesData", [](BMMQ::Modding::HookContext& context) {
        context.userData += 1u;
    }) != 0u);
    BMMQ::Modding::HookContext context{};
    assert(host.invokeHook("SpeciesData", context));
    assert(context.userData == 1u);

    bytes[1] = 0x55u;
    const auto state = host.exportState();
    bytes[1] = 0u;
    assert(host.importState(state));
    bytes = host.region(regionId);
    assert(bytes[1] == 0x55u);
    host.reset();
    assert(bytes[0] == 0u && bytes[1] == 0u);
    return 0;
}
