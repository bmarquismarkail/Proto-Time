#include "cores/gameboy/GameBoyMapper.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

namespace {

std::vector<uint8_t> makeRom(uint8_t cartridgeType, uint8_t ramSizeCode)
{
    std::vector<uint8_t> rom(0x100000u, 0x00u);
    rom[0x0147u] = cartridgeType;
    rom[0x0148u] = 0x05u;
    rom[0x0149u] = ramSizeCode;
    return rom;
}

std::vector<uint8_t> makeTruncatedRom(std::size_t size, uint8_t cartridgeType)
{
    std::vector<uint8_t> rom(size, 0x00u);
    if (rom.size() > 0x0147u) {
        rom[0x0147u] = cartridgeType;
    }
    if (rom.size() > 0x0148u) {
        rom[0x0148u] = 0x00u;
    }
    if (rom.size() > 0x0149u) {
        rom[0x0149u] = 0x00u;
    }
    return rom;
}

std::vector<uint8_t> makeMbc5Rom(std::size_t bankCount)
{
    std::vector<uint8_t> rom(bankCount * 0x4000u, 0x00u);
    rom[0x0147u] = 0x19u;
    rom[0x0148u] = 0x00u;
    rom[0x0149u] = 0x00u;
    return rom;
}

std::vector<uint8_t> makeBankedRom(std::size_t bankCount, uint8_t type, uint8_t romSizeCode)
{
    std::vector<uint8_t> rom(bankCount * 0x4000u, 0x00u);
    for (std::size_t bank = 0; bank < bankCount; ++bank) {
        std::fill_n(rom.begin() + static_cast<std::ptrdiff_t>(bank * 0x4000u),
                    0x4000u,
                    static_cast<uint8_t>(bank));
    }
    rom[0x0147u] = type;
    rom[0x0148u] = romSizeCode;
    rom[0x0149u] = 0x00u;
    return rom;
}

} // namespace

int main()
{
    const auto rom = makeRom(0x03u, 0x01u);
    const auto metadata = GB::parseCartridgeMetadata(rom);
    assert(metadata.mapper == GB::CartridgeMapper::MBC1);
    assert(metadata.externalRamSize == 0x0800u);

    GB::GameBoyMapper mapper;
    mapper.load(rom);
    assert(mapper.write(0x0000u, 0x0Au).handled);

    std::array<uint8_t, 1> writeByte{0x5Au};
    assert(mapper.ramWrite(0xA7FFu, std::span<const uint8_t>(writeByte.data(), writeByte.size())));
    assert(!mapper.ramWrite(0xA800u, std::span<const uint8_t>(writeByte.data(), writeByte.size())));

    std::array<uint8_t, 1> readByte{0x00u};
    assert(mapper.ramRead(0xA7FFu, std::span<uint8_t>(readByte.data(), readByte.size())));
    assert(readByte[0] == 0x5Au);

    readByte[0] = 0xCCu;
    assert(!mapper.ramRead(0xA800u, std::span<uint8_t>(readByte.data(), readByte.size())));
    assert(readByte[0] == 0xCCu);

    const auto save = mapper.extractDirtySaveSnapshot();
    assert(save.externalRam.size() == metadata.externalRamSize);
    assert(save.externalRam[0x07FFu] == 0x5Au);

    const auto state = mapper.exportState();
    assert(state.romSize == rom.size());
    assert(state.romBankCount == mapper.romBankCount());
    assert(state.ramEnabled);
    assert(state.dirty);
    assert(state.externalRam.size() == metadata.externalRamSize);
    assert(state.externalRam[0x07FFu] == 0x5Au);

    GB::GameBoyMapper restoredMapper;
    restoredMapper.load(rom);
    restoredMapper.importState(state);
    readByte[0] = 0x00u;
    assert(restoredMapper.ramRead(0xA7FFu, std::span<uint8_t>(readByte.data(), readByte.size())));
    assert(readByte[0] == 0x5Au);

    auto invalidState = state;
    invalidState.romSize += 1u;
    bool rejected = false;
    try {
        restoredMapper.importState(invalidState);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    GB::GameBoyMapper truncatedMapper;
    truncatedMapper.load(makeTruncatedRom(0x2000u, 0x00u));

    std::array<uint8_t, 16> window{};
    window.fill(0x11u);
    truncatedMapper.copyRomBankWindow(1u, std::span<uint8_t>(window.data(), window.size()));
    for (uint8_t byte : window) {
        assert(byte == 0xFFu);
    }

    truncatedMapper.copyRomBankWindow(0u, std::span<uint8_t>());

    GB::GameBoyMapper mbc5Mapper;
    mbc5Mapper.load(makeMbc5Rom(257u));

    const auto lowWrite = mbc5Mapper.write(0x2000u, 0x00u);
    assert(lowWrite.handled);
    assert(lowWrite.romBankChanged);

    const auto highWrite = mbc5Mapper.write(0x3000u, 0x01u);
    assert(highWrite.handled);
    assert(highWrite.romBankChanged);
    assert(mbc5Mapper.currentRomBank() == 256u);

    const auto mbc1Rom = makeBankedRom(128u, 0x03u, 0x06u);
    GB::GameBoyMapper mbc1Mapper;
    mbc1Mapper.load(mbc1Rom);
    mbc1Mapper.write(0x2000u, 0x01u);
    auto upperWrite = mbc1Mapper.write(0x4000u, 0x01u);
    assert(upperWrite.romBankChanged);
    assert(mbc1Mapper.currentRomBank() == 0x21u);
    const auto modeWrite = mbc1Mapper.write(0x6000u, 0x01u);
    assert(modeWrite.romBankChanged);
    assert(modeWrite.fixedRomBankChanged);
    assert(mbc1Mapper.currentRomBank() == 0x21u);
    assert(mbc1Mapper.currentFixedRomBank() == 0x20u);
    upperWrite = mbc1Mapper.write(0x4000u, 0x02u);
    assert(upperWrite.romBankChanged);
    assert(upperWrite.fixedRomBankChanged);
    assert(mbc1Mapper.currentRomBank() == 0x41u);
    assert(mbc1Mapper.currentFixedRomBank() == 0x40u);

    const auto mbc2Rom = makeBankedRom(4u, 0x05u, 0x01u);
    GB::GameBoyMapper mbc2Mapper;
    mbc2Mapper.load(mbc2Rom);
    assert(mbc2Mapper.write(0x0100u, 0x04u).romBankChanged);
    assert(mbc2Mapper.currentRomBank() == 0u);
    assert(mbc2Mapper.write(0x2000u, 0x0Au).handled);
    assert(mbc2Mapper.ramWrite(0xA000u, std::span<const uint8_t>(writeByte.data(), writeByte.size())));
    assert(mbc2Mapper.write(0x2000u, 0x00u).handled);
    assert(!mbc2Mapper.ramWrite(0xA000u, std::span<const uint8_t>(writeByte.data(), writeByte.size())));

    GB::GameBoyCartridge mbc2Cartridge;
    mbc2Cartridge.load(mbc2Rom);
    assert(mbc2Cartridge.write(0x2000u, 0x0Au).handled);
    assert(mbc2Cartridge.write(0xA000u, 0x0Bu).handled);
    assert(mbc2Cartridge.read(0xA000u) == 0xFBu);
    assert(mbc2Cartridge.write(0x2100u, 0x04u).romBankChanged);
    assert(mbc2Cartridge.switchableBankIndex() == 0u);

    return 0;
}
