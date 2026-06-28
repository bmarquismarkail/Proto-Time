#include <cassert>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "cores/gameboy/GameBoyAPU.hpp"

using GameBoyAPU = GB::GameBoyAPU;

int main()
{
    // Test 1: Freshly created APU should export default state.
    {
        GameBoyAPU apu;
        auto state = apu.exportState();
        assert(state.masterEnabled);
        assert(std::all_of(state.recentSamples.begin(), state.recentSamples.end(), [](int16_t sample) {
            return sample == 0;
        }));
        assert(state.recentWriteCursor == 0u);
        assert(state.recentSampleCount == 0u);
        assert(state.frameCounter == 0u);
        assert(std::all_of(state.waveRam.begin(), state.waveRam.end(), [](uint8_t value) {
            return value == 0;
        }));
        assert(state.nr50 == 0);
        assert(state.nr51 == 0);
        assert(state.pulse1.enabled == false);
        assert(state.pulse2.enabled == false);
        assert(state.wave.enabled == false);
        assert(state.noise.enabled == false);
    }

    // Test 2: Export and import back matches default state.
    {
        GameBoyAPU apu;
        auto exported = apu.exportState();
        GameBoyAPU imported;
        imported.importState(exported);

        auto reExported = imported.exportState();
        assert(reExported.masterEnabled == exported.masterEnabled);
        assert(reExported.frameCounter == exported.frameCounter);
        assert(reExported.recentSamples == exported.recentSamples);
        assert(reExported.waveRam == exported.waveRam);
        assert(reExported.nr50 == exported.nr50);
        assert(reExported.nr51 == exported.nr51);
        assert(reExported.pulse1 == exported.pulse1);
        assert(reExported.pulse2 == exported.pulse2);
        assert(reExported.wave == exported.wave);
        assert(reExported.noise == exported.noise);
    }

    // Test 3: Modify APU registers, export, then import and verify.
    {
        GameBoyAPU apu;

        // Set up a pulse channel (NR10-NR14).
        apu.writeRegister(0xFF10, 0x80);  // NR10: Ch1 on, L-ch length off, ch2 on
        apu.writeRegister(0xFF11, 0x80);  // NR11: Ch1 volume, sweep off
        apu.writeRegister(0xFF12, 0xF0);  // NR12: Ch1 envelope
        apu.writeRegister(0xFF13, 0x00);  // NR13: Ch1 period LSB
        apu.writeRegister(0xFF14, 0x87);  // NR14: Ch1 period MSB, sweep

        // Set up a wave channel (NR30-NR34).
        apu.writeRegister(0xFF1A, 0x80);  // NR30: DAC on
        apu.writeRegister(0xFF1B, 0x20);  // NR31: length
        apu.writeRegister(0xFF1C, 0x60);  // NR32: output level
        apu.writeRegister(0xFF1D, 0x80);  // NR33: frequency LSB
        apu.writeRegister(0xFF1E, 0x87);  // NR34: trigger

        // Set up noise channel (NR41-NR44).
        apu.writeRegister(0xFF20, 0x20);  // NR41: length
        apu.writeRegister(0xFF21, 0xF2);  // NR42: envelope
        apu.writeRegister(0xFF22, 0x15);  // NR43: polynomial counter
        apu.writeRegister(0xFF23, 0x80);  // NR44: trigger

        // Wave RAM (0xFF30-0xFF3F).
        for (uint8_t i = 0; i < 0x10; ++i) {
            apu.writeRegister(0xFF30 + i, static_cast<uint8_t>(i));
        }

        // NR50-NR52 master control.
        apu.writeRegister(0xFF24, 0x77);  // NR50
        apu.writeRegister(0xFF25, 0xF2);  // NR51
        apu.writeRegister(0xFF26, 0x80);  // NR52 master enable

        auto exported = apu.exportState();

        // Verify non-default values were captured.
        assert(exported.pulse1.duty == 2);
        assert(exported.nr52 == 0x80);
        assert(exported.nr50 == 0x77);
        assert(exported.nr51 == 0xF2);
        // Wave RAM should not be all zeros (we wrote 0x00..0x0F).
        const auto* waveData = &exported.waveRam[0];
        assert(waveData[0] == 0x00);
        assert(waveData[1] == 0x01);
        assert(waveData[0x0F] == 0x0F);

        // Import back and verify state matches.
        GameBoyAPU imported;
        imported.importState(exported);
        auto reExported = imported.exportState();

        assert(reExported.masterEnabled == exported.masterEnabled);
        assert(reExported.frameCounter == exported.frameCounter);
        assert(reExported.recentSamples == exported.recentSamples);
        assert(reExported.recentWriteCursor == exported.recentWriteCursor);
        assert(reExported.recentSampleCount == exported.recentSampleCount);
        assert(reExported.pendingReadCursor == exported.pendingReadCursor);
        assert(reExported.pendingSampleCount == exported.pendingSampleCount);
        assert(reExported.waveRam == exported.waveRam);
        assert(reExported.nr50 == exported.nr50);
        assert(reExported.nr51 == exported.nr51);
        assert(reExported.pulse1 == exported.pulse1);
        assert(reExported.pulse2 == exported.pulse2);
        assert(reExported.wave == exported.wave);
        assert(reExported.noise == exported.noise);
    }

    return 0;
}
