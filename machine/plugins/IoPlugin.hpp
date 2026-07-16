#ifndef BMMQ_IO_PLUGIN_HPP
#define BMMQ_IO_PLUGIN_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "../VideoDebugModel.hpp"
#include "../RuntimeContext.hpp"
#include "../AudioPipeline.hpp"
#include "video/RealtimeVideoSurface.hpp"

namespace BMMQ {

class Machine;
class AudioService;
class InputService;
class VideoService;
class VisualOverrideService;
class TimingService;
struct CpuFeedback;
struct VideoStateView;
std::optional<VideoStateView> queryVideoStateSnapshot(const Machine& machine);
std::optional<uint32_t> queryDigitalInputMask(const Machine& machine);
std::optional<VideoDebugFrameModel> queryVideoDebugFrameModel(const Machine& machine,
                                                              const VideoDebugRenderRequest& request);
AudioService& queryAudioService(Machine& machine);
const AudioService& queryAudioService(const Machine& machine);
InputService& queryInputService(Machine& machine);
const InputService& queryInputService(const Machine& machine);
VideoService& queryVideoService(Machine& machine);
const VideoService& queryVideoService(const Machine& machine);
VisualOverrideService& queryVisualOverrideService(Machine& machine);
const VisualOverrideService& queryVisualOverrideService(const Machine& machine);
std::vector<int16_t> queryRecentAudioSamples(const Machine& machine);
uint32_t queryAudioSampleRate(const Machine& machine);
uint8_t queryAudioChannelCount(const Machine& machine);
uint64_t queryAudioFrameCounter(const Machine& machine);
struct RealtimeVideoPacket;
struct RealtimeVideoSubmission;
struct RealtimeAudioPacket;
std::optional<RealtimeVideoSubmission> queryRealtimeVideoPacket(
    const Machine& machine,
    const VideoDebugRenderRequest& request);
std::optional<RealtimeAudioPacket> queryRealtimeAudioPacket(const Machine& machine);
struct SlimVideoPacket;
struct SlimAudioPacket;
std::optional<SlimVideoPacket> querySlimVideoPacket(const Machine& machine);
std::optional<SlimAudioPacket> querySlimAudioPacket(const Machine& machine);
TimingService& queryTimingService(Machine& machine);
const TimingService& queryTimingService(const Machine& machine);

enum class PluginCategory : uint8_t {
    System = 0,
    Video = 1,
    Audio = 2,
    DigitalInput = 3,
    AnalogInput = 4,
    Network = 5,
    Serial = 6,
    Parallel = 7,
};

enum class MachineEventType : uint8_t {
    Attached = 0,
    Detached = 1,
    StepCompleted = 2,
    VBlank = 3,
    AudioFrameReady = 4,
    DigitalInputChanged = 5,
    AnalogInputChanged = 6,
    NetworkActivity = 7,
    SerialActivity = 8,
    ParallelActivity = 9,
    MemoryWriteObserved = 10,
    BootRomVisibilityChanged = 11,
    RomLoaded = 12,
    VideoScanlineReady = 13,
    VisualResourceObserved = 14,
    VisualResourceDecoded = 15,
    VisualOverrideResolved = 16,
    VisualPackMiss = 17,
    FrameCompositionStarted = 18,
    FrameCompositionCompleted = 19,
};

struct VideoUploadRegion {
    std::uint16_t x = 0u;
    std::uint16_t y = 0u;
    std::uint16_t width = 0u;
    std::uint16_t height = 0u;
};

struct RealtimeVideoDiagnostics {
    int width = 0;
    int height = 0;
    bool displayEnabled = false;
    bool inVBlank = false;
    std::optional<std::uint16_t> scanlineIndex;
    struct VdpRenderBodyTiming {
        std::uint64_t totalNs = 0;
        std::uint64_t setupNs = 0;
        std::uint64_t backgroundNs = 0;
        std::uint64_t backgroundSimpleNs = 0;
        std::uint64_t backgroundGeneralNs = 0;
        std::uint64_t backgroundTmsNs = 0;
        std::uint64_t spriteProbeNs = 0;
        std::uint64_t spriteOverlayNs = 0;
        std::uint64_t otherNs = 0;
        std::uint64_t simple_bitplaneDecodeNs = 0;
        std::uint64_t simple_paletteFbWriteNs = 0;
        std::uint64_t simple_loopOtherNs = 0;
    } vdpRenderBodyTiming{};
    struct VdpMode4BackgroundAttributeStats {
        std::uint64_t tileCellsProcessed = 0;
        std::uint64_t tileCellsFlipH = 0;
        std::uint64_t tileCellsFlipV = 0;
        std::uint64_t tileCellsPalette1 = 0;
        std::uint64_t tileCellsPriority = 0;
        std::uint64_t tileCellsFixedTopRows = 0;
        std::uint64_t tileCellsFixedRightColumns = 0;
        std::uint64_t tileCellsLeftBlankOrFineSkip = 0;
        std::uint64_t tileCellsCommonCaseEligible = 0;
        std::uint64_t commonCaseEligiblePixelsWritten = 0;
    } vdpMode4BackgroundAttributes{};
    struct VdpMode4SimpleBackgroundStats {
        std::uint64_t simplePathFrameCount = 0;
        std::uint64_t simplePathRowsRendered = 0;
        std::uint64_t simplePathPixelsWritten = 0;
        std::uint64_t simplePathTileEntriesDecoded = 0;
        std::uint64_t simplePathPatternRowsDecoded = 0;
        std::uint64_t simplePathScrollXAlignedCount = 0;
        std::uint64_t simplePathScrollYValueChanges = 0;
        std::uint64_t simplePathUniqueTileRowsSeen = 0;
        std::uint64_t mode4SimplePathUsedCount = 0;
        std::uint64_t mode4GeneralPathUsedCount = 0;
        std::uint64_t tmsGraphicsPathUsedCount = 0;
    } vdpMode4SimpleBackground{};
};

struct RealtimeVideoPacket {
    static constexpr std::uint16_t kContractVersion = 3u;
    std::uint16_t contractVersion = kContractVersion;
    MachineEventType eventType = MachineEventType::VBlank;
    int width = 0;
    int height = 0;
    bool displayEnabled = false;
    bool inVBlank = false;
    std::optional<std::uint16_t> scanlineIndex;
    std::uint64_t generation = 0;
    std::uint64_t lifecycleEpoch = 1u;
    std::uint64_t producedAtNs = 0u;
    RealtimeVideoSurface surface;
    std::array<VideoUploadRegion, 8u> uploadHints{};
    std::uint8_t uploadHintCount = 0u;

    [[nodiscard]] bool empty() const noexcept
    {
        return width <= 0 ||
               height <= 0 ||
               !surface.validForDimensions(width, height);
    }

    [[nodiscard]] std::size_t pixelCount() const noexcept
    {
        return empty() ? 0u : static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }

    [[nodiscard]] std::size_t payloadBytes() const noexcept
    {
        return surface.payloadBytes();
    }
};

struct RealtimeVideoSubmission {
    RealtimeVideoPacket packet{};
    RealtimeVideoDiagnostics diagnostics{};
};

struct RealtimeAudioPacket {
    static constexpr std::uint16_t kContractVersion = 2u;
    std::uint16_t contractVersion = kContractVersion;
    std::uint32_t sampleRate = 48000u;
    std::uint8_t channelCount = 1u;
    std::uint64_t frameCounter = 0;
    std::uint64_t psgChunksEmitted = 0;
    std::uint64_t psgSamplesGeneratedTotal = 0;
    std::uint32_t psgChunkSamplesLast = 0;
    std::uint32_t psgChunkSamplesMin = 0;
    std::uint32_t psgChunkSamplesMax = 0;
    std::uint32_t psgPendingSamples = 0;
    std::uint64_t firstSampleFrame = 0u;
    std::vector<std::int16_t> pcmSamples;
    std::vector<PsgVoiceDescriptor> voices;
    std::vector<std::int16_t> voiceStems;
    std::vector<PsgAudioEvent> events;

    [[nodiscard]] bool empty() const noexcept
    {
        return pcmSamples.empty();
    }
};

static_assert(offsetof(RealtimeVideoPacket, contractVersion) == 0u,
              "RealtimeVideoPacket contract header must be first");
static_assert(offsetof(RealtimeAudioPacket, contractVersion) == 0u,
              "RealtimeAudioPacket contract header must be first");

struct IoRegionDescriptor {
    PluginCategory category = PluginCategory::System;
    uint16_t start = 0;
    uint16_t size = 0;
    std::string_view label{};
    bool readable = true;
    bool writable = false;
};

struct VideoStateView {
    IoRegionDescriptor vramRegion{};
    IoRegionDescriptor oamRegion{};
    IoRegionDescriptor registerRegion{};
    std::vector<uint8_t> vram;
    std::vector<uint8_t> oam;
    uint8_t lcdc = 0;
    uint8_t stat = 0;
    uint8_t scy = 0;
    uint8_t scx = 0;
    uint8_t ly = 0;
    uint8_t lyc = 0;
    uint8_t bgp = 0xFCu;
    uint8_t obp0 = 0xFFu;
    uint8_t obp1 = 0xFFu;
    uint8_t wy = 0;
    uint8_t wx = 0;

    [[nodiscard]] bool lcdEnabled() const {
        return (lcdc & 0x80u) != 0;
    }

    [[nodiscard]] bool inVBlank() const {
        return ly >= 144u;
    }
};

struct AudioStateView {
    IoRegionDescriptor registerRegion{};
    std::vector<uint8_t> registers;
    std::vector<uint8_t> waveRam;
    std::vector<int16_t> pcmSamples;
    uint32_t sampleRate = 48000;
    uint8_t channelCount = 1;
    uint64_t frameCounter = 0;
    uint8_t nr10 = 0;
    uint8_t nr11 = 0;
    uint8_t nr12 = 0;
    uint8_t nr13 = 0;
    uint8_t nr14 = 0;
    uint8_t nr21 = 0;
    uint8_t nr22 = 0;
    uint8_t nr23 = 0;
    uint8_t nr24 = 0;
    uint8_t nr30 = 0;
    uint8_t nr31 = 0;
    uint8_t nr32 = 0;
    uint8_t nr33 = 0;
    uint8_t nr34 = 0;
    uint8_t nr41 = 0;
    uint8_t nr42 = 0;
    uint8_t nr43 = 0;
    uint8_t nr44 = 0;
    uint8_t nr50 = 0;
    uint8_t nr51 = 0;
    uint8_t nr52 = 0;

    [[nodiscard]] bool soundEnabled() const {
        return (nr52 & 0x80u) != 0;
    }

    [[nodiscard]] bool hasPcmSamples() const {
        return !pcmSamples.empty();
    }
};

struct SerialStateView {
    IoRegionDescriptor registerRegion{};
    std::vector<uint8_t> registers;
    uint8_t sb = 0;
    uint8_t sc = 0;

    [[nodiscard]] bool transferRequested() const {
        return (sc & 0x80u) != 0;
    }

    [[nodiscard]] bool useInternalClock() const {
        return (sc & 0x01u) != 0;
    }
};

struct DigitalInputStateView {
    IoRegionDescriptor registerRegion{};
    uint8_t joypRegister = 0xFFu;
    uint8_t pressedMask = 0;

    [[nodiscard]] bool actionSelected() const {
        return (joypRegister & 0x20u) == 0;
    }

    [[nodiscard]] bool directionSelected() const {
        return (joypRegister & 0x10u) == 0;
    }

    [[nodiscard]] bool actionPressed() const {
        return (pressedMask & 0xF0u) != 0;
    }

    [[nodiscard]] bool directionPressed() const {
        return (pressedMask & 0x0Fu) != 0;
    }

    [[nodiscard]] bool anyPressed() const {
        return pressedMask != 0;
    }
};

struct NetworkStateView {
    bool connected = false;
    bool linkActive = false;
    uint32_t pendingPackets = 0;
    std::string_view detail = "placeholder";
};

struct ParallelStateView {
    bool connected = false;
    bool busy = false;
    uint32_t queuedBytes = 0;
    std::string_view detail = "placeholder";
};

// Legacy tooling snapshot representation. The production real-time path uses
// RealtimeVideoPacket::surface; this type is retained for MachineView clients.
struct VideoDirtyRegion {
    uint16_t start = 0;
    uint16_t size = 0;
    std::vector<uint8_t> bytes;

    [[nodiscard]] bool empty() const noexcept {
        return bytes.empty() || size == 0;
    }
};

// Legacy tooling packet. Dirty regions currently cover full VRAM/OAM and are
// intentionally not used by the emulation-to-render transport.
struct SlimVideoPacket {
    static constexpr std::uint16_t kContractVersion = 1u;
    std::uint16_t contractVersion = kContractVersion;
    std::vector<VideoDirtyRegion> dirtyRegions;
    bool displayEnabled = false;
    bool inVBlank = false;
    uint8_t lcdc = 0;
    uint8_t stat = 0;
    uint8_t ly = 0;

    [[nodiscard]] bool empty() const noexcept {
        return dirtyRegions.empty();
    }
};

// Slim audio packet: contiguous PCM samples with minimal metadata.
// Avoids re-deriving full AudioStateView when only PCM is needed.
struct SlimAudioPacket {
    static constexpr std::uint16_t kContractVersion = 1u;
    std::uint16_t contractVersion = kContractVersion;
    std::vector<std::int16_t> pcmSamples;
    uint32_t sampleRate = 48000;
    uint8_t channelCount = 1;
    uint64_t frameCounter = 0;

    [[nodiscard]] bool empty() const noexcept {
        return pcmSamples.empty();
    }
};

struct MachineView {
    const Machine& machine;
    const RuntimeContext& runtime;
    std::vector<IoRegionDescriptor> ioRegions;

    [[nodiscard]] const AudioService& audioService() const {
        return queryAudioService(machine);
    }

    [[nodiscard]] const InputService& inputService() const {
        return queryInputService(machine);
    }

    [[nodiscard]] const VideoService& videoService() const {
        return queryVideoService(machine);
    }

    [[nodiscard]] const VisualOverrideService& visualOverrideService() const {
        return queryVisualOverrideService(machine);
    }

    [[nodiscard]] const TimingService& timingService() const {
        return queryTimingService(machine);
    }

    [[nodiscard]] uint8_t read8(uint16_t address) const {
        return runtime.peek8(address);
    }

    [[nodiscard]] uint16_t read16(uint16_t address) const {
        return runtime.peek16(address);
    }

    [[nodiscard]] uint8_t readRegister8(std::string_view id) const {
        return runtime.readRegister8(id);
    }

    [[nodiscard]] uint16_t readRegister16(std::string_view id) const {
        return runtime.readRegister16(id);
    }

    [[nodiscard]] std::vector<uint8_t> readRegion(uint16_t start, uint16_t size) const {
        std::vector<uint8_t> bytes;
        bytes.reserve(size);
        for (uint32_t offset = 0; offset < size; ++offset) {
            bytes.push_back(read8(static_cast<uint16_t>(start + offset)));
        }
        return bytes;
    }

    [[nodiscard]] std::optional<IoRegionDescriptor> findRegion(PluginCategory category,
                                                               std::string_view label = {}) const {
        for (const auto& region : ioRegions) {
            if (region.category != category) {
                continue;
            }
            if (!label.empty() && region.label != label) {
                continue;
            }
            return region;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<VideoStateView> videoState() const {
        if (auto snapshot = queryVideoStateSnapshot(machine); snapshot.has_value()) {
            return snapshot;
        }
        const auto vramRegion = findRegion(PluginCategory::Video, "VRAM");
        const auto oamRegion = findRegion(PluginCategory::Video, "OAM");
        const auto registerRegion = findRegion(PluginCategory::Video, "LCD Registers");
        if (!vramRegion.has_value() || !oamRegion.has_value() || !registerRegion.has_value()) {
            return std::nullopt;
        }

        VideoStateView state;
        state.vramRegion = *vramRegion;
        state.oamRegion = *oamRegion;
        state.registerRegion = *registerRegion;
        state.vram = readRegion(vramRegion->start, vramRegion->size);
        state.oam = readRegion(oamRegion->start, oamRegion->size);
        state.lcdc = read8(0xFF40u);
        state.stat = read8(0xFF41u);
        state.scy = read8(0xFF42u);
        state.scx = read8(0xFF43u);
        state.ly = read8(0xFF44u);
        state.lyc = read8(0xFF45u);
        state.bgp = read8(0xFF47u);
        state.obp0 = read8(0xFF48u);
        state.obp1 = read8(0xFF49u);
        state.wy = read8(0xFF4Au);
        state.wx = read8(0xFF4Bu);
        return state;
    }

    [[nodiscard]] std::optional<VideoDebugFrameModel> videoDebugFrameModel(
        const VideoDebugRenderRequest& request) const
    {
        return queryVideoDebugFrameModel(machine, request);
    }

    [[nodiscard]] std::optional<RealtimeVideoSubmission> realtimeVideoPacket(
        const VideoDebugRenderRequest& request) const
    {
        return queryRealtimeVideoPacket(machine, request);
    }

    [[nodiscard]] std::optional<AudioStateView> audioState() const {
        const auto registerRegion = findRegion(PluginCategory::Audio, "APU Registers");
        if (!registerRegion.has_value()) {
            return std::nullopt;
        }

        AudioStateView state;
        state.registerRegion = *registerRegion;
        state.registers = readRegion(registerRegion->start, registerRegion->size);
        state.waveRam = readRegion(0xFF30u, 0x0010u);
        state.pcmSamples = queryRecentAudioSamples(machine);
        state.sampleRate = queryAudioSampleRate(machine);
        state.channelCount = queryAudioChannelCount(machine);
        state.frameCounter = queryAudioFrameCounter(machine);
        state.nr10 = read8(0xFF10u);
        state.nr11 = read8(0xFF11u);
        state.nr12 = read8(0xFF12u);
        state.nr13 = read8(0xFF13u);
        state.nr14 = read8(0xFF14u);
        state.nr21 = read8(0xFF16u);
        state.nr22 = read8(0xFF17u);
        state.nr23 = read8(0xFF18u);
        state.nr24 = read8(0xFF19u);
        state.nr30 = read8(0xFF1Au);
        state.nr31 = read8(0xFF1Bu);
        state.nr32 = read8(0xFF1Cu);
        state.nr33 = read8(0xFF1Du);
        state.nr34 = read8(0xFF1Eu);
        state.nr41 = read8(0xFF20u);
        state.nr42 = read8(0xFF21u);
        state.nr43 = read8(0xFF22u);
        state.nr44 = read8(0xFF23u);
        state.nr50 = read8(0xFF24u);
        state.nr51 = read8(0xFF25u);
        state.nr52 = read8(0xFF26u);
        return state;
    }

    [[nodiscard]] std::optional<RealtimeAudioPacket> realtimeAudioPacket() const
    {
        return queryRealtimeAudioPacket(machine);
    }

    // Slim video state: returns memory regions with their bytes plus minimal LCD state,
    // avoiding full VideoStateView construction and ARGB frame copies.
    // Currently emits full VRAM/OAM spans; future write tracking can narrow these.
    [[nodiscard]] std::optional<SlimVideoPacket> videoDirtyRegions() const
    {
        const auto vramRegion = findRegion(PluginCategory::Video, "VRAM");
        const auto oamRegion = findRegion(PluginCategory::Video, "OAM");

        SlimVideoPacket packet;
        packet.displayEnabled = (read8(0xFF40u) & 0x80u) != 0;
        packet.lcdc = read8(0xFF40u);
        packet.stat = read8(0xFF41u);
        packet.ly = read8(0xFF44u);
        packet.inVBlank = packet.ly >= 144u;

        if (vramRegion.has_value()) {
            VideoDirtyRegion vramDirty;
            vramDirty.start = vramRegion->start;
            const auto byteCount = static_cast<uint16_t>(std::min<uint32_t>(vramRegion->size, 0x2000u));
            vramDirty.size = byteCount;
            vramDirty.bytes.reserve(byteCount);
            for (uint32_t offset = 0; offset < byteCount; ++offset) {
                vramDirty.bytes.push_back(read8(static_cast<uint16_t>(vramRegion->start + offset)));
            }
            packet.dirtyRegions.push_back(std::move(vramDirty));
        }

        if (oamRegion.has_value()) {
            VideoDirtyRegion oamDirty;
            oamDirty.start = oamRegion->start;
            const auto byteCount = static_cast<uint16_t>(std::min<uint32_t>(oamRegion->size, 0xA0u));
            oamDirty.size = byteCount;
            oamDirty.bytes.reserve(byteCount);
            for (uint32_t offset = 0; offset < byteCount; ++offset) {
                oamDirty.bytes.push_back(read8(static_cast<uint16_t>(oamRegion->start + offset)));
            }
            packet.dirtyRegions.push_back(std::move(oamDirty));
        }

        return packet;
    }

    // Slim audio state: returns only contiguous PCM samples with minimal metadata,
    // avoiding the full register+WAVE+NRxx copy that audioState() performs.
    [[nodiscard]] std::optional<SlimAudioPacket> realtimeSlimAudioPacket() const
    {
        SlimAudioPacket packet;
        packet.sampleRate = queryAudioSampleRate(machine);
        packet.channelCount = queryAudioChannelCount(machine);
        packet.frameCounter = queryAudioFrameCounter(machine);
        packet.pcmSamples = queryRecentAudioSamples(machine);
        return packet;
    }

    [[nodiscard]] std::optional<SerialStateView> serialState() const {
        const auto registerRegion = findRegion(PluginCategory::Serial, "Serial Registers");
        if (!registerRegion.has_value()) {
            return std::nullopt;
        }

        SerialStateView state;
        state.registerRegion = *registerRegion;
        state.sb = read8(0xFF01u);
        state.sc = read8(0xFF02u);
        state.registers = {state.sb, state.sc};
        return state;
    }

    [[nodiscard]] std::optional<DigitalInputStateView> digitalInputState() const {
        const auto registerRegion = findRegion(PluginCategory::DigitalInput, "Joypad");
        if (!registerRegion.has_value()) {
            return std::nullopt;
        }

        DigitalInputStateView state;
        state.registerRegion = *registerRegion;
        state.joypRegister = read8(0xFF00u);
        if (const auto pressedMask = queryDigitalInputMask(machine); pressedMask.has_value()) {
            state.pressedMask = static_cast<uint8_t>(*pressedMask & 0x00FFu);
            return state;
        }
        const auto lowNibble = static_cast<uint8_t>(~state.joypRegister) & 0x0Fu;
        if (state.directionSelected()) {
            state.pressedMask = static_cast<uint8_t>(state.pressedMask | lowNibble);
        }
        if (state.actionSelected()) {
            state.pressedMask = static_cast<uint8_t>(state.pressedMask | static_cast<uint8_t>(lowNibble << 4));
        }
        return state;
    }

    [[nodiscard]] std::optional<NetworkStateView> networkState() const {
        return NetworkStateView{};
    }

    [[nodiscard]] std::optional<ParallelStateView> parallelState() const {
        return ParallelStateView{};
    }
};

struct MutableMachineView : MachineView {
    Machine& mutableMachine;

    MutableMachineView(Machine& machine,
                       RuntimeContext& runtime,
                       std::vector<IoRegionDescriptor> ioRegionsIn)
        : MachineView{machine, runtime, std::move(ioRegionsIn)},
          mutableMachine(machine) {}

    [[nodiscard]] AudioService& audioService() {
        return queryAudioService(mutableMachine);
    }

    [[nodiscard]] const AudioService& audioService() const {
        return queryAudioService(mutableMachine);
    }

    [[nodiscard]] VideoService& videoService() {
        return queryVideoService(mutableMachine);
    }

    [[nodiscard]] const VideoService& videoService() const {
        return queryVideoService(mutableMachine);
    }

    [[nodiscard]] VisualOverrideService& visualOverrideService() {
        return queryVisualOverrideService(mutableMachine);
    }

    [[nodiscard]] const VisualOverrideService& visualOverrideService() const {
        return queryVisualOverrideService(mutableMachine);
    }

    [[nodiscard]] InputService& inputService() {
        return queryInputService(mutableMachine);
    }

    [[nodiscard]] const InputService& inputService() const {
        return queryInputService(mutableMachine);
    }

    [[nodiscard]] TimingService& timingService() {
        return queryTimingService(mutableMachine);
    }
};

struct MachineEvent {
    MachineEventType type = MachineEventType::StepCompleted;
    PluginCategory category = PluginCategory::System;
    uint64_t tick = 0;
    uint16_t address = 0;
    uint8_t value = 0;
    const CpuFeedback* feedback = nullptr;
    std::string_view detail{};
};

class IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual std::string_view id() const = 0;
    virtual std::string_view displayName() const {
        return id();
    }
    virtual void onAttach(MutableMachineView&) {}
    virtual void onDetach(MutableMachineView&) {}
    virtual void onMachineEvent(const MachineEvent&, const MachineView&) {}
};

class IVideoPlugin : public virtual IPlugin {
public:
    virtual void onVideoEvent(const MachineEvent&, const MachineView&) = 0;
};

class IAudioPlugin : public virtual IPlugin {
public:
    virtual void onAudioEvent(const MachineEvent&, const MachineView&) = 0;
};

class IDigitalInputPlugin : public virtual IPlugin {
public:
    virtual std::optional<uint32_t> sampleDigitalInput(const MachineView&) {
        return std::nullopt;
    }
    virtual void onDigitalInputEvent(const MachineEvent&, const MachineView&) {}
};

class IAnalogInputPlugin : public virtual IPlugin {
public:
    using AnalogState = std::vector<float>;

    virtual std::optional<AnalogState> sampleAnalogInput(const MachineView&) {
        return std::nullopt;
    }
    virtual void onAnalogInputEvent(const MachineEvent&, const MachineView&) {}
};

class INetworkPlugin : public virtual IPlugin {
public:
    virtual void onNetworkEvent(const MachineEvent&, const MachineView&) = 0;
};

class ISerialPlugin : public virtual IPlugin {
public:
    virtual void onSerialEvent(const MachineEvent&, const MachineView&) = 0;
};

class IParallelPlugin : public virtual IPlugin {
public:
    virtual void onParallelEvent(const MachineEvent&, const MachineView&) = 0;
};

} // namespace BMMQ

#endif // BMMQ_IO_PLUGIN_HPP
