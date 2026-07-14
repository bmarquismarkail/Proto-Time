// smoke_slim_packets.cpp - verifies VideoDirtyRegion, SlimVideoPacket,
// SlimAudioPacket, and their Machine/MachineView query paths.

#include <cassert>
#include <cstdint>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
using GameBoyMachine = GB::GameBoyMachine;
#include "machine/Machine.hpp"
#include "machine/plugins/IoPlugin.hpp"

int main()
{
    // Production video transport is self-contained, lossless, and materially
    // smaller than an ARGB framebuffer for the palettes used by 8-bit cores.
    const std::vector<std::uint32_t> sourcePixels = {
        0xFF000000u, 0xFFFFFFFFu, 0xFF00FF00u, 0xFF000000u,
        0xFFFFFFFFu, 0xFF00FF00u, 0xFF000000u, 0xFFFFFFFFu,
    };
    const auto packed = BMMQ::packVideoPixels(sourcePixels);
    assert(packed.validForPixelCount(sourcePixels.size()));
    assert(packed.bitsPerPixel == 2u);
    assert(packed.payloadBytes() < sourcePixels.size() * sizeof(std::uint32_t));
    std::vector<std::uint32_t> decodedPixels;
    assert(BMMQ::unpackVideoPixels(packed, sourcePixels.size(), decodedPixels));
    assert(decodedPixels == sourcePixels);

    // VideoDirtyRegion defaults
    BMMQ::VideoDirtyRegion region;
    assert(region.empty());

    region.start = 0x8000;
    region.size = 256;
    region.bytes.assign(256, 0xAB);
    assert(!region.empty());
    assert(region.bytes.size() == 256);

    region.bytes.clear();
    assert(region.empty());

    region.bytes.push_back(0);
    region.size = 0;
    assert(region.empty()); // size 0 counts as empty

    // SlimVideoPacket defaults
    BMMQ::SlimVideoPacket slimVideo;
    assert(slimVideo.empty());
    assert(slimVideo.dirtyRegions.empty());
    assert(slimVideo.contractVersion == BMMQ::SlimVideoPacket::kContractVersion);
    assert(!slimVideo.displayEnabled);
    assert(slimVideo.lcdc == 0);

    // SlimVideoPacket with data
    BMMQ::VideoDirtyRegion vramDirty;
    vramDirty.start = 0x8000;
    vramDirty.size = 0x2000;
    vramDirty.bytes.resize(0x2000, 0x00);
    slimVideo.dirtyRegions.push_back(std::move(vramDirty));
    assert(!slimVideo.empty());
    assert(slimVideo.dirtyRegions.size() == 1);
    assert(slimVideo.dirtyRegions[0].start == 0x8000);
    assert(slimVideo.dirtyRegions[0].size == 0x2000);

    // SlimAudioPacket defaults
    BMMQ::SlimAudioPacket slimAudio;
    assert(slimAudio.empty());
    assert(slimAudio.pcmSamples.empty());
    assert(slimAudio.contractVersion == BMMQ::SlimAudioPacket::kContractVersion);
    assert(slimAudio.sampleRate == 48000);
    assert(slimAudio.channelCount == 1);

    // SlimAudioPacket with data
    BMMQ::SlimAudioPacket slimAudioData;
    slimAudioData.pcmSamples = {100, 200, 300, 400};
    slimAudioData.sampleRate = 44100;
    slimAudioData.channelCount = 2;
    slimAudioData.frameCounter = 12345;
    assert(!slimAudioData.empty());
    assert(slimAudioData.pcmSamples.size() == 4);
    assert(slimAudioData.sampleRate == 44100);
    assert(slimAudioData.channelCount == 2);
    assert(slimAudioData.frameCounter == 12345);

    // MachineView::videoDirtyRegions with GameBoyMachine
    GameBoyMachine machine;
    std::vector<uint8_t> emptyRom(256, 0x00);
    machine.loadRom(emptyRom);

    BMMQ::Machine& host = machine;
    auto view = host.view();
    auto viewSlimVideo = view.videoDirtyRegions();
    assert(viewSlimVideo.has_value());
    assert(!viewSlimVideo->empty());
    assert(viewSlimVideo->dirtyRegions.size() >= 1);

    bool hasDataRegion = false;
    for (const auto& r : viewSlimVideo->dirtyRegions) {
        if (!r.bytes.empty()) { hasDataRegion = true; break; }
    }
    assert(hasDataRegion);

    // MachineView::realtimeSlimAudioPacket with GameBoyMachine
    auto viewSlimAudio = view.realtimeSlimAudioPacket();
    assert(viewSlimAudio.has_value());
    assert(viewSlimAudio->sampleRate == 48000);
    assert(viewSlimAudio->channelCount == 1);

    // Machine::realtimeSlimAudioPacket
    auto machineSlimAudio = host.realtimeSlimAudioPacket();
    assert(machineSlimAudio.has_value());
    assert(machineSlimAudio->sampleRate == 48000);
    assert(machineSlimAudio->channelCount == 1);

    // Machine::realtimeSlimVideoPacket returns nullopt (not overridden in GameBoyMachine)
    auto machineSlimVideo = host.realtimeSlimVideoPacket();
    assert(!machineSlimVideo.has_value());

     // Query functions (const Machine reference)
    const BMMQ::Machine& constHost = machine;
    auto qVideo = BMMQ::querySlimVideoPacket(constHost);
    auto qAudio = BMMQ::querySlimAudioPacket(constHost);
    assert(!qVideo.has_value());
    assert(qAudio.has_value());
    assert(qAudio->sampleRate == 48000);

    // Test with a real cartridge ROM to verify region addresses
    std::vector<uint8_t> cartridgeRom(0x8000, 0x00);
    cartridgeRom[0x0100] = 0x3E;
    cartridgeRom[0x0101] = 0x12;
    cartridgeRom[0x0102] = 0x00;
    machine.loadRom(cartridgeRom);

    auto view2 = host.view();
    auto slim2 = view2.videoDirtyRegions();
    assert(slim2.has_value());

    // Find VRAM region and check address
    const BMMQ::VideoDirtyRegion* vram = nullptr;
    const BMMQ::VideoDirtyRegion* oam = nullptr;
    for (const auto& r : slim2->dirtyRegions) {
        if (r.size == 0x2000) vram = &r;
        if (r.size == 0xA0) oam = &r;
    }
    assert(vram != nullptr);
    assert(oam != nullptr);
    // VRAM should start at 0x8000
    assert(vram->start == 0x8000);
    assert(vram->bytes.size() == 0x2000);
    // OAM should start at 0xFE00
    assert(oam->start == 0xFE00);
    assert(oam->bytes.size() == 0xA0);

    // Write to VRAM and verify dirty region reflects the change
    host.runtimeContext().write8(0x8000, 0xAA);
    host.runtimeContext().write8(0x8001, 0xBB);
    auto slim3 = view2.videoDirtyRegions();
    assert(slim3.has_value());
    const BMMQ::VideoDirtyRegion* vram3 = nullptr;
    for (const auto& r : slim3->dirtyRegions) {
        if (r.size == 0x2000) { vram3 = &r; break; }
    }
    assert(vram3 != nullptr);
    assert(vram3->bytes[0] == 0xAA);
    assert(vram3->bytes[1] == 0xBB);

    return 0;
}
