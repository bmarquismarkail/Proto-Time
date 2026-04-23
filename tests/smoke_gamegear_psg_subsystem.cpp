#include "cores/gamegear/GameGearPSG.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace {

bool hasNonZero(const std::vector<int16_t>& samples)
{
    return std::any_of(samples.begin(), samples.end(), [](int16_t sample) {
        return sample != 0;
    });
}

}

int main()
{
    GameGearPSG psg;
    psg.reset();

    assert(psg.sampleRate() == 48000u);
    assert(psg.frameCounter() == 0u);
    assert(psg.copyRecentSamples().empty());

    psg.writeCompatRegister(0xFF26u, 0x80u);
    psg.writeCompatRegister(0xFF12u, 0xF0u);
    psg.writeCompatRegister(0xFF13u, 0x01u);
    psg.writeCompatRegister(0xFF14u, 0x80u);

    assert(psg.readCompatRegister(0xFF26u) & 0x80u);
    assert(psg.readCompatRegister(0xFF26u) & 0x01u);

    for (int i = 0; i < 30000 && psg.frameCounter() == 0u; ++i) {
        psg.step(4u);
    }

    assert(psg.frameCounter() >= 1u);
    const auto recent = psg.copyRecentSamples();
    assert(!recent.empty());
    assert(hasNonZero(recent));

    psg.writeWaveRam(0xFF30u, 0xABu);
    psg.writeWaveRam(0xFF3Fu, 0xCDu);
    assert(psg.readWaveRam(0xFF30u) == 0xABu);
    assert(psg.readWaveRam(0xFF3Fu) == 0xCDu);

    psg.writeCompatRegister(0xFF26u, 0x00u);
    assert((psg.readCompatRegister(0xFF26u) & 0x80u) == 0u);

    return 0;
}
