#include "cores/gamegear/GameGearPSG.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

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

    if (!check(psg.sampleRate() == 48000u, "PSG sample rate changed")) return 1;
    if (!check(psg.outputChannelCount() == 2u, "Game Gear PSG should publish stereo PCM")) return 1;
    if (!check(psg.frameCounter() == 0u, "PSG reset should clear frame counter")) return 1;
    if (!check(psg.copyRecentSamples().empty(), "PSG reset should clear recent samples")) return 1;
    if (!check(psg.stereoControl() == 0xFFu, "PSG reset should route all channels to both speakers")) return 1;

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
    assert((recent.size() % 2u) == 0u);

    psg.reset();
    psg.writeData(0x80u | 0x04u); // tone 0 low nibble = 4
    psg.writeData(0x12u);         // tone 0 high bits = 0x12
    psg.writeData(0x90u);         // tone 0 attenuation = loudest
    assert(psg.tonePeriod(0u) == 0x124u);
    assert(psg.channelAttenuation(0u) == 0u);
    assert((psg.readCompatRegister(0xFF26u) & 0x01u) != 0u);

    psg.writeData(0xE5u); // noise control: white noise, tone-2 linked rate
    assert(psg.noiseControl() == 0x05u);
    psg.writeData(0xF0u); // noise attenuation = loudest
    assert(psg.channelAttenuation(3u) == 0u);

    psg.writeStereoControl(0x10u); // tone 0 left only
    for (int i = 0; i < 30000 && psg.frameCounter() == 0u; ++i) {
        psg.step(4u);
    }
    const auto leftOnly = psg.copyRecentSamples();
    assert(!leftOnly.empty());
    assert((leftOnly.size() % 2u) == 0u);
    assert(hasNonZero(leftOnly));
    for (std::size_t i = 1; i < leftOnly.size(); i += 2u) {
        assert(leftOnly[i] == 0);
    }

    psg.writeStereoControl(0x00u);
    const auto frameBeforeMute = psg.frameCounter();
    for (int i = 0; i < 30000 && psg.frameCounter() == frameBeforeMute; ++i) {
        psg.step(4u);
    }
    const auto muted = psg.copyRecentSamples();
    assert(!muted.empty());
    for (const auto sample : muted) {
        assert(sample == 0);
    }

    psg.writeWaveRam(0xFF30u, 0xABu);
    psg.writeWaveRam(0xFF3Fu, 0xCDu);
    assert(psg.readWaveRam(0xFF30u) == 0xABu);
    assert(psg.readWaveRam(0xFF3Fu) == 0xCDu);

    psg.writeCompatRegister(0xFF26u, 0x00u);
    assert((psg.readCompatRegister(0xFF26u) & 0x80u) == 0u);

    return 0;
}
