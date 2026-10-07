#pragma once
#include <cstddef>
#include <cstdint>

// Optional owner-lane inspection, separately versioned from GameGearMapper.
// Offsets describe physical storage even while another bank is mapped. Reads
// inspect arrays without executing cartridge or device operations.
class IGameGearMapperRamV1 {
public:
    virtual ~IGameGearMapperRamV1() = default;
    virtual std::size_t physicalRamCapacityV1() const noexcept = 0;
    virtual bool mappedRamOffsetV1(uint16_t, std::size_t&) const noexcept = 0;
    virtual bool physicalRamByteV1(std::size_t, uint8_t&) const noexcept = 0;
};
