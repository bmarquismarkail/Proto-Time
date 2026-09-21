#include "emulator/PokemonRedTelemetry.hpp"
#include "machine/RuntimeContext.hpp"
#include <bit>
#include <openssl/sha.h>

namespace BMMQ {
bool supportsPokemonRedTelemetry(std::span<const std::uint8_t> rom) {
    if (rom.size() != 1048576) return false;
    constexpr std::array<unsigned char, SHA_DIGEST_LENGTH> expected{
        0xea,0x9b,0xca,0xe6,0x17,0xfd,0xf1,0x59,0xb0,0x45,
        0x18,0x54,0x67,0xae,0x58,0xb2,0xe4,0xa4,0x8b,0x9a};
    std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
    SHA1(rom.data(), rom.size(), digest.data());
    return digest == expected;
}

PokemonRedSnapshot PokemonRedSnapshot::capture(const RuntimeContext& runtime) {
    std::array<std::uint8_t, 0x2000> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = runtime.peek8(static_cast<std::uint16_t>(0xc000 + i));
    return PokemonRedSnapshot(bytes);
}

RiverXmbTelemetry PokemonRedSnapshot::telemetry() const {
    const auto byte = [&](unsigned address) { return wram_[address - 0xc000]; };
    const auto word = [&](unsigned address) -> unsigned {
        return (unsigned(byte(address)) << 8) | byte(address + 1);
    };
    RiverXmbTelemetry result;
    const unsigned count = byte(0xd163);
    // Reject boot/title RAM and partially updated or malformed party/time data.
    if (!(byte(0xd732) & 1) || count > 6 || byte(0xd164 + count) != 0xff ||
        byte(0xda43) >= 60 || byte(0xda44) >= 60 || byte(0xda45) >= 60)
        return result;
    const auto mapName = [](unsigned map) -> std::string {
        switch (map) {
#include "emulator/PokemonRedMapNames.inc"
        default: return {};
        }
    };
    result.location = mapName(byte(0xd35e));
    if (result.location.empty()) return {};
    result.badges = std::popcount(unsigned(byte(0xd356)));
    result.playTime = unsigned(byte(0xda41)) * 3600 + unsigned(byte(0xda43)) * 60 + byte(0xda44);
    for (unsigned i = 0; i < count; ++i) {
        const unsigned base = 0xd16b + 44 * i;
        RiverXmbPartyMember member;
        member.species = byte(base);
        member.level = byte(base + 33);
        member.hp = word(base + 1);
        member.maxHp = word(base + 34);
        if (member.species == 0 || member.species > 190 || member.species != byte(0xd164 + i)) return {};
        // Battle HP changes before the corresponding party record is synchronized.
        // Preserve party identity (Transform changes battle species), and avoid
        // the previous battle's buffer until the first monsters are out.
        if ((byte(0xd057) == 1 || byte(0xd057) == 2) && byte(0xd11d) == 0 && byte(0xcc2f) == i) {
            member.hp = word(0xd015);
            member.maxHp = word(0xd023);
        }
        if (!member.level || member.level > 100 || !member.maxHp || member.maxHp > 999 || member.hp > member.maxHp) return {};
        bool terminated = false;
        for (unsigned n = 0; n < 11; ++n) {
            const auto c = byte(0xd2b5 + 11 * i + n);
            if (c == 0x50) { terminated = true; break; }
            if (c >= 0x80 && c <= 0x99) member.name += char('A' + c - 0x80);
            else if (c >= 0xa0 && c <= 0xb9) member.name += char('a' + c - 0xa0);
            else if (c >= 0xf6) member.name += char('0' + c - 0xf6);
            else if (c == 0x7f) member.name += ' ';
            else if (c == 0xe0) member.name += '\'';
            else if (c == 0xe3) member.name += '-';
            else if (c == 0xe7) member.name += '!';
            else if (c == 0xe8 || c == 0xf2) member.name += '.';
            else if (c == 0xf3) member.name += '/';
            else if (c == 0xf4) member.name += ',';
            else if (c == 0xef) member.name += "♂";
            else if (c == 0xf5) member.name += "♀";
            else member.name += '?';
        }
        if (!terminated) return {};
        if (member.name.empty()) member.name = "Species " + std::to_string(member.species);
        result.party.push_back(std::move(member));
    }
    result.available = true;
    return result;
}
} // namespace BMMQ
