#ifndef GB_HARDWARE_REGISTERS_HPP
#define GB_HARDWARE_REGISTERS_HPP

#include <array>
#include <cstdint>
#include <string_view>

#include "../../memory/reg_base.hpp"

namespace GB::HardwareRegisters {

struct HardwareRegisterSpec {
    std::string_view name;
    uint16_t address;
};

inline constexpr auto kSpecs = std::to_array<HardwareRegisterSpec>({
    {"JOYP", 0xFF00},
    {"SB", 0xFF01},
    {"SC", 0xFF02},
    {"DIV", 0xFF04},
    {"TIMA", 0xFF05},
    {"TMA", 0xFF06},
    {"TAC", 0xFF07},
    {"IF", 0xFF0F},
    {"NR10", 0xFF10},
    {"NR11", 0xFF11},
    {"NR12", 0xFF12},
    {"NR13", 0xFF13},
    {"NR14", 0xFF14},
    {"NR21", 0xFF16},
    {"NR22", 0xFF17},
    {"NR23", 0xFF18},
    {"NR24", 0xFF19},
    {"NR30", 0xFF1A},
    {"NR31", 0xFF1B},
    {"NR32", 0xFF1C},
    {"NR33", 0xFF1D},
    {"NR34", 0xFF1E},
    {"NR41", 0xFF20},
    {"NR42", 0xFF21},
    {"NR43", 0xFF22},
    {"NR44", 0xFF23},
    {"NR50", 0xFF24},
    {"NR51", 0xFF25},
    {"NR52", 0xFF26},
    {"WAVE_RAM_0", 0xFF30},
    {"WAVE_RAM_1", 0xFF31},
    {"WAVE_RAM_2", 0xFF32},
    {"WAVE_RAM_3", 0xFF33},
    {"WAVE_RAM_4", 0xFF34},
    {"WAVE_RAM_5", 0xFF35},
    {"WAVE_RAM_6", 0xFF36},
    {"WAVE_RAM_7", 0xFF37},
    {"WAVE_RAM_8", 0xFF38},
    {"WAVE_RAM_9", 0xFF39},
    {"WAVE_RAM_A", 0xFF3A},
    {"WAVE_RAM_B", 0xFF3B},
    {"WAVE_RAM_C", 0xFF3C},
    {"WAVE_RAM_D", 0xFF3D},
    {"WAVE_RAM_E", 0xFF3E},
    {"WAVE_RAM_F", 0xFF3F},
    {"LCDC", 0xFF40},
    {"STAT", 0xFF41},
    {"SCY", 0xFF42},
    {"SCX", 0xFF43},
    {"LY", 0xFF44},
    {"LYC", 0xFF45},
    {"DMA", 0xFF46},
    {"BGP", 0xFF47},
    {"OBP0", 0xFF48},
    {"OBP1", 0xFF49},
    {"WY", 0xFF4A},
    {"WX", 0xFF4B},
    {"KEY0_SYS", 0xFF4C},
    {"KEY1_SPD", 0xFF4D},
    {"VBK", 0xFF4F},
    {"BANK", 0xFF50},
    {"HDMA1", 0xFF51},
    {"HDMA2", 0xFF52},
    {"HDMA3", 0xFF53},
    {"HDMA4", 0xFF54},
    {"HDMA5", 0xFF55},
    {"RP", 0xFF56},
    {"BCPS_BGPI", 0xFF68},
    {"BCPD_BGPD", 0xFF69},
    {"OCPS_OBPI", 0xFF6A},
    {"OCPD_OBPD", 0xFF6B},
    {"OPRI", 0xFF6C},
    {"SVBK_WBK", 0xFF70},
    {"PCM12", 0xFF76},
    {"PCM34", 0xFF77},
    {"IE", 0xFFFF},
});

inline void registerIn(BMMQ::RegisterFile<uint16_t>& regfile)
{
    for (const auto& spec : kSpecs) {
        regfile.addRegister(BMMQ::RegisterDescriptor{
            std::string(spec.name),
            BMMQ::RegisterWidth::Byte8,
            BMMQ::RegisterStorage::AddressMapped,
            spec.address,
            false,
        });
    }
}

} // namespace GB::HardwareRegisters

#endif // GB_HARDWARE_REGISTERS_HPP
