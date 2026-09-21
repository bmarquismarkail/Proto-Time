#include "emulator/PokemonRedTelemetry.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include <cassert>
#include <nlohmann/json.hpp>

int main() {
    GB::GameBoyMachine machine;
    std::vector<std::uint8_t> rom(32768);
    machine.loadRom(rom);
    auto& runtime = machine.runtimeContext();
    const auto put = [&](unsigned a, unsigned v) { runtime.write8(a, v); };
    const auto word = [&](unsigned a, unsigned v) { put(a, v >> 8); put(a + 1, v & 255); };
    const auto capture = [&] { return BMMQ::PokemonRedSnapshot::capture(runtime).telemetry(); };
    assert(!BMMQ::supportsPokemonRedTelemetry(rom));
    rom.resize(1048576);
    std::copy_n("POKEMON RED", 11, rom.begin() + 0x134);
    assert(!BMMQ::supportsPokemonRedTelemetry(rom)); // Spoofed header is insufficient.
    assert(BMMQ::simulatedRiverXmbTelemetry(1).location == "Viridian City");
    assert(!capture().available);
    for (unsigned a = 0xc000; a < 0xe000; ++a) put(a, 0);
    put(0xd732, 1);
    put(0xd163, 6);
    put(0xd16a, 255);
    put(0xd35e, 1);
    put(0xd356, 0xa5);
    put(0xda41, 2); put(0xda43, 3); put(0xda44, 4);
    for (unsigned i = 0; i < 6; ++i) {
        const unsigned base = 0xd16b + 44 * i;
        put(0xd164 + i, 153); put(base, 153);
        word(base + 1, i == 0 ? 0 : 257 + i);
        put(base + 33, 50 + i); word(base + 34, 400 + i);
        put(0xd2b5 + 11 * i, 0x80 + i); put(0xd2b6 + 11 * i, 0x50);
    }
    const auto frozen = BMMQ::PokemonRedSnapshot::capture(runtime);
    auto data = frozen.telemetry();
    assert(data.available && data.location == "Viridian City");
    assert(data.playTime == 7384 && data.badges == 4 && data.party.size() == 6);
    assert(data.party[0].hp == 0 && data.party[1].hp == 258 && data.party[5].maxHp == 405);
    assert(data.party[5].name == "F" && data.party[5].level == 55);
    put(0xda41, 255); put(0xda42, 255); put(0xda43, 0); put(0xda44, 0);
    assert(capture().playTime == 255 * 3600); // Maxed flag is not a second hours byte.
    put(0xda41, 2); put(0xda42, 0); put(0xda43, 3); put(0xda44, 4);
    put(0xd35e, 0); word(0xd16c, 12);
    assert(frozen.telemetry().location == "Viridian City" && frozen.telemetry().party[0].hp == 0);
    assert(capture().location == "Pallet Town" && capture().party[0].hp == 12);
    put(0xd057, 1); put(0xcc2f, 1);
    word(0xd015, 7); word(0xd023, 401);
    assert(capture().party[1].hp == 7);
    put(0xd11d, 1);
    assert(capture().party[1].hp == 258); // Battle setup must not use stale battle bytes.
    put(0xd057, 0);
    put(0xd163, 7); assert(!capture().available);
    put(0xd163, 6); put(0xd16a, 0); assert(!capture().available);
    put(0xd16a, 255); put(0xda44, 60); assert(!capture().available);
    put(0xda44, 4); word(0xd16c, 401); assert(!capture().available);
    word(0xd16c, 12); put(0xd732, 0); assert(!capture().available);
    put(0xd732, 1); put(0xd35e, 255); assert(!capture().available);
    put(0xd35e, 0x25); assert(capture().location == "Reds House 1F");
    put(0xd163, 0); put(0xd164, 255);
    assert(capture().available && capture().party.empty());
    const auto payload = nlohmann::json::parse(BMMQ::RiverXmbIntegration::telemetryJson(data));
    assert(payload["telemetry"]["party"].size() == 6);
    assert(payload["telemetry"]["party"][1]["hp"] == 258);
    assert(nlohmann::json::parse(BMMQ::RiverXmbIntegration::telemetryJson({}))["telemetry"]["party"].empty());
}
