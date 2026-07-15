#include "GameBoyMachine.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "../../inst_cycle/executor/PluginContract.hpp"
#include "../../machine/SaveState.hpp"
#include "../../machine/plugins/IoPlugin.hpp"
#include "../../machine/plugins/PluginManager.hpp"
#include "../../machine/BackgroundTaskService.hpp"
#include "cartridge/CartridgeSaveManager.hpp"
#include "hardware_registers.hpp"
#include "register_id.hpp"
#include "video/GameBoyVisualDebugAdapter.hpp"

namespace GB {

// Expose Game Boy memory-mapped I/O descriptors.
static constexpr std::size_t kMaxRomSize = 1024u * 1024u;

constexpr std::array<BMMQ::IoRegionDescriptor, 7> kIoRegions{{
    {BMMQ::PluginCategory::Video, 0x8000u, 0x2000u, "VRAM", true, true},
    {BMMQ::PluginCategory::Video, 0xFE00u, 0x00A0u, "OAM", true, true},
    {BMMQ::PluginCategory::Video, 0xFF40u, 0x000Cu, "LCD Registers", true, true},
    {BMMQ::PluginCategory::Audio, 0xFF10u, 0x0017u, "APU Registers", true, true},
    {BMMQ::PluginCategory::Audio, 0xFF30u, 0x0010u, "Wave RAM", true, true},
    {BMMQ::PluginCategory::DigitalInput, 0xFF00u, 0x0001u, "Joypad", true, true},
    {BMMQ::PluginCategory::Serial, 0xFF01u, 0x0002u, "Serial Registers", true, true},
}};

[[nodiscard]] inline bool romPathAllowsSaveBinding(const std::optional<std::filesystem::path>& path) {
    if (!path.has_value()) return false;
    if (!path->has_filename()) return false;
    auto extension = path->extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension != ".sms"; // Game Boy ROMs allow saves, SMS typically doesn't
}

[[nodiscard]] inline std::optional<CartridgeSaveManager::SaveSnapshot> flushSaveSnapshotViaBackground(
    BMMQ::BackgroundTaskService& backgroundTaskService,
    CartridgeSaveManager::SaveSnapshot snapshot)
{
    auto sharedSnapshot = std::make_shared<CartridgeSaveManager::SaveSnapshot>(std::move(snapshot));
    const bool queued = backgroundTaskService.submit(BMMQ::BackgroundJobCategory::SaveFlush, [sharedSnapshot]() {
        CartridgeSaveManager::flushSnapshot(*sharedSnapshot);
    });
    if (!queued) {
        return std::move(*sharedSnapshot);
    }
    return std::nullopt;
}

class StateWriter {
public:
    [[nodiscard]] const std::vector<uint8_t>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<uint8_t> take() { return std::move(bytes_); }

    void u8(uint8_t value) { bytes_.push_back(value); }
    void boolean(bool value) { u8(value ? 1u : 0u); }
    void u16(uint16_t value) {
        u8(static_cast<uint8_t>(value & 0xFFu));
        u8(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    }
    void u32(uint32_t value) {
        u16(static_cast<uint16_t>(value & 0xFFFFu));
        u16(static_cast<uint16_t>((value >> 16u) & 0xFFFFu));
    }
    void u64(uint64_t value) {
        u32(static_cast<uint32_t>(value & 0xFFFFFFFFull));
        u32(static_cast<uint32_t>((value >> 32u) & 0xFFFFFFFFull));
    }
    void size(std::size_t value) {
        if (value > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("save state section too large");
        }
        u32(static_cast<uint32_t>(value));
    }
    void bytes(std::span<const uint8_t> data) {
        size(data.size());
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
    void raw(std::span<const uint8_t> data) {
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
    void i16(int16_t value) { u16(static_cast<uint16_t>(value)); }

private:
    std::vector<uint8_t> bytes_{};
};

class StateReader {
public:
    explicit StateReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool done() const noexcept { return pos_ == bytes_.size(); }

    uint8_t u8() {
        require(1u);
        return bytes_[pos_++];
    }
    bool boolean() {
        const auto value = u8();
        if (value > 1u) {
            throw std::invalid_argument("save state boolean is invalid");
        }
        return value != 0u;
    }
    uint16_t u16() {
        const auto lo = static_cast<uint16_t>(u8());
        const auto hi = static_cast<uint16_t>(u8());
        return static_cast<uint16_t>(lo | (hi << 8u));
    }
    uint32_t u32() {
        const auto lo = static_cast<uint32_t>(u16());
        const auto hi = static_cast<uint32_t>(u16());
        return lo | (hi << 16u);
    }
    uint64_t u64() {
        const auto lo = static_cast<uint64_t>(u32());
        const auto hi = static_cast<uint64_t>(u32());
        return lo | (hi << 32u);
    }
    std::size_t size() { return static_cast<std::size_t>(u32()); }
    std::vector<uint8_t> bytes(std::size_t maxSize = std::numeric_limits<std::size_t>::max()) {
        const auto count = size();
        if (count > maxSize) {
            throw std::invalid_argument("save state vector section is too large");
        }
        require(count);
        std::vector<uint8_t> out(bytes_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                 bytes_.begin() + static_cast<std::ptrdiff_t>(pos_ + count));
        pos_ += count;
        return out;
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }

private:
    void require(std::size_t count) const {
        if (pos_ > bytes_.size() || count > bytes_.size() - pos_) {
            throw std::invalid_argument("save state section is truncated");
        }
    }

    std::span<const uint8_t> bytes_;
    std::size_t pos_ = 0;
};

template <std::size_t N>
void writeInt16Array(StateWriter& writer, const std::array<int16_t, N>& values)
{
    writer.size(values.size());
    for (const auto value : values) {
        writer.i16(value);
    }
}

template <std::size_t N>
void readInt16Array(StateReader& reader, std::array<int16_t, N>& values)
{
    const auto count = reader.size();
    if (count != values.size()) {
        throw std::invalid_argument("save state int16 array size mismatch");
    }
    for (auto& value : values) {
        value = reader.i16();
    }
}

template <std::size_t N>
void writeU8Array(StateWriter& writer, const std::array<uint8_t, N>& values)
{
    writer.size(values.size());
    writer.raw(std::span<const uint8_t>(values.data(), values.size()));
}

template <std::size_t N>
void readU8Array(StateReader& reader, std::array<uint8_t, N>& values)
{
    const auto bytes = reader.bytes(values.size());
    if (bytes.size() != values.size()) {
        throw std::invalid_argument("save state uint8 array size mismatch");
    }
    std::copy(bytes.begin(), bytes.end(), values.begin());
}

void writeCpuFeedback(StateWriter& writer, const BMMQ::CpuFeedback& feedback)
{
    writer.boolean(feedback.segmentBoundaryHint);
    writer.boolean(feedback.isControlFlow);
    writer.u32(feedback.pcBefore);
    writer.u32(feedback.pcAfter);
    writer.u32(feedback.retiredCycles);
    writer.u32(static_cast<uint32_t>(feedback.executionPath));
}

BMMQ::CpuFeedback readCpuFeedback(StateReader& reader)
{
    BMMQ::CpuFeedback feedback;
    feedback.segmentBoundaryHint = reader.boolean();
    feedback.isControlFlow = reader.boolean();
    feedback.pcBefore = reader.u32();
    feedback.pcAfter = reader.u32();
    feedback.retiredCycles = reader.u32();
    const auto path = reader.u32();
    if (path > static_cast<uint32_t>(BMMQ::ExecutionPathHint::CpuOptimizedFastPath)) {
        throw std::invalid_argument("save state execution path invalid");
    }
    feedback.executionPath = static_cast<BMMQ::ExecutionPathHint>(path);
    return feedback;
}

std::vector<uint8_t> serializeCpuState(const LR3592_DMG::SaveState& state)
{
    StateWriter writer;
    writer.u16(state.af);
    writer.u16(state.bc);
    writer.u16(state.de);
    writer.u16(state.hl);
    writer.u16(state.sp);
    writer.u16(state.pc);
    writer.u16(state.flagset);
    writeCpuFeedback(writer, state.feedback);
    writer.u8(state.cip);
    writer.boolean(state.ime);
    writer.boolean(state.imeEnablePending);
    writer.u8(state.imeEnableDelay);
    writer.boolean(state.stopFlag);
    writer.boolean(state.haltFlag);
    writer.boolean(state.haltBugActive);
    writer.boolean(state.haltBugPcAdjustPending);
    writer.u16(state.dividerCounter);
    writer.boolean(state.dmaActive);
    writer.u16(state.dmaSourceBase);
    writer.u16(state.dmaCycleProgress);
    writer.u64(static_cast<uint64_t>(state.pendingCycleCharge));
    writer.boolean(state.serialTransferActive);
    writer.u16(state.serialCycleProgress);
    writer.u8(state.joypSelect);
    writer.u8(state.joypadPressedMask);
    writer.u32(state.ppuDotCounter);
    writer.boolean(state.lcdEnabledLastTick);
    writer.boolean(state.statInterruptLatched);
    writer.u16(state.currentVramBank);
    writer.u8(state.spriteContext);
    writer.boolean(state.bankSwitchingEnabled);
    return writer.take();
}

LR3592_DMG::SaveState deserializeCpuState(std::span<const uint8_t> bytes)
{
    StateReader reader(bytes);
    LR3592_DMG::SaveState state;
    state.af = reader.u16();
    state.bc = reader.u16();
    state.de = reader.u16();
    state.hl = reader.u16();
    state.sp = reader.u16();
    state.pc = reader.u16();
    state.flagset = reader.u16();
    state.feedback = readCpuFeedback(reader);
    state.cip = reader.u8();
    state.ime = reader.boolean();
    state.imeEnablePending = reader.boolean();
    state.imeEnableDelay = reader.u8();
    state.stopFlag = reader.boolean();
    state.haltFlag = reader.boolean();
    state.haltBugActive = reader.boolean();
    state.haltBugPcAdjustPending = reader.boolean();
    state.dividerCounter = reader.u16();
    state.dmaActive = reader.boolean();
    state.dmaSourceBase = reader.u16();
    state.dmaCycleProgress = reader.u16();
    state.pendingCycleCharge = static_cast<std::size_t>(reader.u64());
    state.serialTransferActive = reader.boolean();
    state.serialCycleProgress = reader.u16();
    state.joypSelect = reader.u8();
    state.joypadPressedMask = reader.u8();
    state.ppuDotCounter = reader.u32();
    state.lcdEnabledLastTick = reader.boolean();
    state.statInterruptLatched = reader.boolean();
    state.currentVramBank = reader.u16();
    state.spriteContext = reader.u8();
    state.bankSwitchingEnabled = reader.boolean();
    if (!reader.done()) {
        throw std::invalid_argument("CPU save state has trailing data");
    }
    return state;
}

void writePulseChannel(StateWriter& writer, const PulseChannel& channel)
{
    writer.boolean(channel.enabled);
    writer.boolean(channel.dacEnabled);
    writer.boolean(channel.lengthEnabled);
    writer.u8(channel.duty);
    writer.u8(channel.dutyStep);
    writer.u16(channel.lengthCounter);
    writer.u8(channel.initialVolume);
    writer.u8(channel.volume);
    writer.boolean(channel.envelopeIncrease);
    writer.u8(channel.envelopePeriod);
    writer.u8(channel.envelopeTimer);
    writer.u16(channel.frequency);
    writer.u16(channel.timer);
    writer.boolean(channel.hasSweep);
    writer.u8(channel.sweepPeriod);
    writer.u8(channel.sweepTimer);
    writer.boolean(channel.sweepNegate);
    writer.u8(channel.sweepShift);
    writer.u16(channel.shadowFrequency);
    writer.boolean(channel.sweepEnabled);
}

PulseChannel readPulseChannel(StateReader& reader)
{
    PulseChannel channel;
    channel.enabled = reader.boolean();
    channel.dacEnabled = reader.boolean();
    channel.lengthEnabled = reader.boolean();
    channel.duty = reader.u8();
    channel.dutyStep = reader.u8();
    channel.lengthCounter = reader.u16();
    channel.initialVolume = reader.u8();
    channel.volume = reader.u8();
    channel.envelopeIncrease = reader.boolean();
    channel.envelopePeriod = reader.u8();
    channel.envelopeTimer = reader.u8();
    channel.frequency = reader.u16();
    channel.timer = reader.u16();
    channel.hasSweep = reader.boolean();
    channel.sweepPeriod = reader.u8();
    channel.sweepTimer = reader.u8();
    channel.sweepNegate = reader.boolean();
    channel.sweepShift = reader.u8();
    channel.shadowFrequency = reader.u16();
    channel.sweepEnabled = reader.boolean();
    return channel;
}

void writeWaveChannel(StateWriter& writer, const WaveChannel& channel)
{
    writer.boolean(channel.enabled);
    writer.boolean(channel.dacEnabled);
    writer.boolean(channel.lengthEnabled);
    writer.u16(channel.lengthCounter);
    writer.u16(channel.frequency);
    writer.u16(channel.timer);
    writer.u8(channel.sampleIndex);
    writer.u8(channel.outputLevel);
}

WaveChannel readWaveChannel(StateReader& reader)
{
    WaveChannel channel;
    channel.enabled = reader.boolean();
    channel.dacEnabled = reader.boolean();
    channel.lengthEnabled = reader.boolean();
    channel.lengthCounter = reader.u16();
    channel.frequency = reader.u16();
    channel.timer = reader.u16();
    channel.sampleIndex = reader.u8();
    channel.outputLevel = reader.u8();
    return channel;
}

void writeNoiseChannel(StateWriter& writer, const NoiseChannel& channel)
{
    writer.boolean(channel.enabled);
    writer.boolean(channel.dacEnabled);
    writer.boolean(channel.lengthEnabled);
    writer.u16(channel.lengthCounter);
    writer.u8(channel.initialVolume);
    writer.u8(channel.volume);
    writer.boolean(channel.envelopeIncrease);
    writer.u8(channel.envelopePeriod);
    writer.u8(channel.envelopeTimer);
    writer.u8(channel.clockShift);
    writer.u8(channel.divisorCode);
    writer.boolean(channel.widthMode7);
    writer.u16(channel.timer);
    writer.u16(channel.lfsr);
}

NoiseChannel readNoiseChannel(StateReader& reader)
{
    NoiseChannel channel;
    channel.enabled = reader.boolean();
    channel.dacEnabled = reader.boolean();
    channel.lengthEnabled = reader.boolean();
    channel.lengthCounter = reader.u16();
    channel.initialVolume = reader.u8();
    channel.volume = reader.u8();
    channel.envelopeIncrease = reader.boolean();
    channel.envelopePeriod = reader.u8();
    channel.envelopeTimer = reader.u8();
    channel.clockShift = reader.u8();
    channel.divisorCode = reader.u8();
    channel.widthMode7 = reader.boolean();
    channel.timer = reader.u16();
    channel.lfsr = reader.u16();
    return channel;
}

std::vector<uint8_t> serializeApuState(const GameBoyAPUState& state)
{
    StateWriter writer;
    writer.boolean(state.masterEnabled);
    writer.u32(state.frameSequencerCounter);
    writer.u8(state.frameSequencerStep);
    writer.u32(state.sampleAccumulator);
    writer.u64(state.sampleCounter);
    writer.u64(state.frameCounter);
    writeInt16Array(writer, state.recentSamples);
    writer.size(state.recentWriteCursor);
    writer.size(state.recentSampleCount);
    writer.size(state.pendingReadCursor);
    writer.size(state.pendingSampleCount);
    writeU8Array(writer, state.waveRam);
    writer.u8(state.nr50);
    writer.u8(state.nr51);
    writer.u8(state.nr52);
    writePulseChannel(writer, state.pulse1);
    writePulseChannel(writer, state.pulse2);
    writeWaveChannel(writer, state.wave);
    writeNoiseChannel(writer, state.noise);
    return writer.take();
}

GameBoyAPUState deserializeApuState(std::span<const uint8_t> bytes)
{
    StateReader reader(bytes);
    GameBoyAPUState state;
    state.masterEnabled = reader.boolean();
    state.frameSequencerCounter = reader.u32();
    state.frameSequencerStep = reader.u8();
    state.sampleAccumulator = reader.u32();
    state.sampleCounter = reader.u64();
    state.frameCounter = reader.u64();
    readInt16Array(reader, state.recentSamples);
    state.recentWriteCursor = reader.size();
    state.recentSampleCount = reader.size();
    state.pendingReadCursor = reader.size();
    state.pendingSampleCount = reader.size();
    readU8Array(reader, state.waveRam);
    state.nr50 = reader.u8();
    state.nr51 = reader.u8();
    state.nr52 = reader.u8();
    state.pulse1 = readPulseChannel(reader);
    state.pulse2 = readPulseChannel(reader);
    state.wave = readWaveChannel(reader);
    state.noise = readNoiseChannel(reader);
    if (!reader.done()) {
        throw std::invalid_argument("APU save state has trailing data");
    }
    return state;
}

std::vector<uint8_t> serializeMapperState(const GameBoyMapper::SaveState& state)
{
    StateWriter writer;
    writer.size(state.romSize);
    writer.size(state.romBankCount);
    writer.u16(state.romBankLow);
    writer.size(state.effectiveRomBank);
    writer.u8(state.ramBankSelect);
    writer.u8(state.ramBankMode);
    writer.boolean(state.ramEnabled);
    writer.boolean(state.dirty);
    writer.boolean(state.hasBattery);
    writer.boolean(state.externalRamValid);
    writer.bytes(state.externalRam);
    return writer.take();
}

GameBoyMapper::SaveState deserializeMapperState(std::span<const uint8_t> bytes)
{
    StateReader reader(bytes);
    GameBoyMapper::SaveState state;
    state.romSize = reader.size();
    state.romBankCount = reader.size();
    state.romBankLow = reader.u16();
    state.effectiveRomBank = reader.size();
    state.ramBankSelect = reader.u8();
    state.ramBankMode = reader.u8();
    state.ramEnabled = reader.boolean();
    state.dirty = reader.boolean();
    state.hasBattery = reader.boolean();
    state.externalRamValid = reader.boolean();
    state.externalRam = reader.bytes(0x20000u);
    if (!reader.done()) {
        throw std::invalid_argument("mapper save state has trailing data");
    }
    return state;
}

std::vector<uint8_t> serializeCartridgeState(const GameBoyCartridge::State& state)
{
    StateWriter writer;
    writer.size(state.romSize);
    writer.size(state.romBankCount);
    writer.size(state.currentRomBank);
    writer.size(state.currentRamBank);
    writer.boolean(state.ramEnabled);
    writer.u8(state.selectedRtcRegister);
    writer.boolean(state.rtc.has_value());
    if (state.rtc.has_value()) {
        writeU8Array(writer, state.rtc->registers);
        writer.boolean(state.rtc->latched);
    }
    writer.boolean(state.mbc1BankingModeSelect);
    writer.size(state.mbc1UpperBankBits);
    writer.size(state.mbc1LowBankBits);
    writer.boolean(state.dirty);
    writer.bytes(state.externalRam);
    return writer.take();
}

GameBoyCartridge::State deserializeCartridgeState(std::span<const uint8_t> bytes)
{
    StateReader reader(bytes);
    GameBoyCartridge::State state;
    state.romSize = reader.size();
    state.romBankCount = reader.size();
    state.currentRomBank = reader.size();
    state.currentRamBank = reader.size();
    state.ramEnabled = reader.boolean();
    state.selectedRtcRegister = reader.u8();
    const bool hasRtc = reader.boolean();
    if (hasRtc) {
        RtcSaveData rtc;
        readU8Array(reader, rtc.registers);
        rtc.latched = reader.boolean();
        state.rtc = rtc;
    }
    state.mbc1BankingModeSelect = reader.boolean();
    state.mbc1UpperBankBits = reader.size();
    state.mbc1LowBankBits = reader.size();
    state.dirty = reader.boolean();
    state.externalRam = reader.bytes(0x20000u);
    if (!reader.done()) {
        throw std::invalid_argument("cartridge save state has trailing data");
    }
    return state;
}

BMMQ::SaveStateChunk makeChunk(std::string name, std::vector<uint8_t> data)
{
    BMMQ::SaveStateChunk chunk;
    chunk.name = std::move(name);
    chunk.size = static_cast<uint32_t>(data.size());
    chunk.data = std::move(data);
    return chunk;
}

const BMMQ::SaveStateChunk& requireChunk(const BMMQ::SaveStateFile& state, std::string_view name)
{
    const auto found = std::find_if(state.chunks.begin(), state.chunks.end(), [name](const auto& chunk) {
        return chunk.name == name;
    });
    if (found == state.chunks.end()) {
        throw std::invalid_argument("save state is missing required chunk");
    }
    return *found;
}

// Game Boy runtime context — mirrors GameGearRuntimeContext pattern.
class GameBoyRuntimeContext final : public BMMQ::RuntimeContext,
                                    public BMMQ::ITranslationCapability,
                                    public BMMQ::IInvalidationCapability {
public:
    GameBoyRuntimeContext(LR3592_PluginRuntime& runtime,
                          GameBoyMemoryMap& memoryMap,
                          bool& romLoaded,
                          BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy)
        : runtime_(runtime),
          memoryMap_(memoryMap),
          romLoaded_(romLoaded),
          activePolicy_(activePolicy) {
        refreshExecutionMode();
    }

    FetchBlock fetch() override {
        if (!romLoaded_) {
            throw std::runtime_error("ROM is not loaded");
        }
        return runtime_.fetch();
    }

    ExecutionBlock decode(FetchBlock& fetchBlock) override {
        return runtime_.decode(fetchBlock);
    }

    void execute(const ExecutionBlock& block, FetchBlock& fetchBlock) override {
        runtime_.execute(block, fetchBlock);
    }

    BMMQ::CpuFeedback step(FetchBlock& fetchBlock) override {
        if (fastExecutionAllowed() && runtime_.cpu().tryFastExecute(fetchBlock)) {
            return runtime_.getLastFeedback();
        }
        cachedExecutionBlock_.clear();
        cachedExecutionBlock_.reserve(4);
        runtime_.cpu().decodeInto(fetchBlock, cachedExecutionBlock_);
        runtime_.execute(cachedExecutionBlock_, fetchBlock);
        return runtime_.getLastFeedback();
    }

    BMMQ::CpuFeedback step() override {
        if (!romLoaded_) {
            throw std::runtime_error("ROM is not loaded");
        }
        if (fastExecutionAllowed() && runtime_.cpu().tryExecuteTranslatedBlock()) {
            return runtime_.getLastFeedback();
        }
        runtime_.cpu().fetchInto(cachedFetchBlock_);
        const auto feedback = step(cachedFetchBlock_);
        if (fastExecutionAllowed() &&
            feedback.executionPath == BMMQ::ExecutionPathHint::CpuOptimizedFastPath) {
            runtime_.cpu().populateBlockCache(cachedFetchBlock_);
        }
        return feedback;
    }

    uint8_t read8(uint16_t address) const override {
        return memoryMap_.read(resolveEchoAddress(address));
    }

    uint8_t peek8(uint16_t address) const override {
        address = resolveEchoAddress(address);
        // For addresses that go through cartridge intercept, use read8
        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {
            return read8(address);
        }
        return memoryMap_.read(address);
    }

    void write8(uint16_t address, uint8_t value) override {
        memoryMap_.write(resolveEchoAddress(address), value);
    }

    uint8_t readRegister8(std::string_view id) const override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Byte8) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            return read8(*descriptor.mappedAddress);
        }
        auto* entry = requireRegisterEntry(id);
        return static_cast<uint8_t>(entry->reg->value & 0x00FFu);
    }

    void writeRegister8(std::string_view id, uint8_t value) override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Byte8) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            write8(*descriptor.mappedAddress, value);
            return;
        }
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    uint16_t readRegister16(std::string_view id) const override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Word16) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            return read16(*descriptor.mappedAddress);
        }
        auto* entry = requireRegisterEntry(id);
        return entry->reg->value;
    }

    void writeRegister16(std::string_view id, uint16_t value) override {
        const auto& descriptor = requireDescriptor(id);
        if (descriptor.width != BMMQ::RegisterWidth::Word16) {
            throw std::invalid_argument("register width mismatch");
        }
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {
            if (!descriptor.mappedAddress.has_value()) {
                throw std::invalid_argument("address-backed register missing address");
            }
            write16(*descriptor.mappedAddress, value);
            return;
        }
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    uint16_t readRegisterPair(std::string_view id) const override {
        const auto& descriptor = requireDescriptor(id);
        if (!descriptor.isPair) {
            throw std::invalid_argument("register is not a pair");
        }
        auto* entry = requireRegisterEntry(id);
        return entry->reg->value;
    }

    void writeRegisterPair(std::string_view id, uint16_t value) override {
        const auto& descriptor = requireDescriptor(id);
        if (!descriptor.isPair) {
            throw std::invalid_argument("register is not a pair");
        }
        auto* entry = requireRegisterEntry(id);
        entry->reg->value = value;
    }

    const BMMQ::CpuFeedback& getLastFeedback() const override {
        return runtime_.getLastFeedback();
    }

    uint32_t clockHz() const override {
        return runtime_.cpu().clockHz();
    }

    BMMQ::ExecutionGuarantee guarantee() const override {
        return activePolicy_->guarantee();
    }

    const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override {
        return &activePolicy_->metadata();
    }

    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {
        return *activePolicy_;
    }

    BMMQ::ITranslationCapability* translationCapability() override { return this; }
    BMMQ::IInvalidationCapability* invalidationCapability() override { return this; }
    const BMMQ::ITranslationCapability* translationCapability() const override { return this; }
    const BMMQ::IInvalidationCapability* invalidationCapability() const override { return this; }

    void refreshExecutionMode() {
        allowFastPath_ = activePolicy_ != nullptr &&
            activePolicy_->guarantee() != BMMQ::ExecutionGuarantee::BaselineFaithful;
    }

    [[nodiscard]] bool fastExecutionAllowed() const noexcept {
        return allowFastPath_;
    }

private:
    static uint16_t resolveEchoAddress(uint16_t address) {
        if (address >= 0xE000 && address <= 0xFDFF) {
            return static_cast<uint16_t>(address - 0x2000);
        }
        return address;
    }

    using RegisterEntry = decltype(std::declval<BMMQ::RegisterFile<uint16_t>&>().findRegister("AF"));

    const BMMQ::RegisterDescriptor& requireDescriptor(std::string_view id) const {
        const auto* descriptor = runtime_.cpu().getMemory().file.findDescriptor(id);
        if (descriptor == nullptr) {
            throw std::invalid_argument("register not found");
        }
        return *descriptor;
    }

    RegisterEntry requireRegisterEntry(std::string_view id) const {
        auto* entry = runtime_.cpu().getMemory().file.findRegister(id);
        if (entry == nullptr || entry->reg == nullptr) {
            throw std::invalid_argument("register not found");
        }
        return entry;
    }

    LR3592_PluginRuntime& runtime_;
    GameBoyMemoryMap& memoryMap_;
    FetchBlock cachedFetchBlock_{};
    ExecutionBlock cachedExecutionBlock_{};
    const bool& romLoaded_;
    BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy_;
    bool allowFastPath_ = false;
};


// ---------------------------------------------------------------------------
// GameBoyMachine implementation
// ---------------------------------------------------------------------------
GameBoyMachine::GameBoyMachine() : impl_(std::make_unique<Impl>()) {
    videoService().setVisualDebugAdapter(visualDebugAdapter());

    // Wire CPU memory interface to memory map
    impl_->cpu.cpu().attachMemory(impl_->memoryMap);

    // Wire PPU to memory map for VRAM/OAM reads
    impl_->ppu.memoryMap = &impl_->memoryMap;

    // Set up mapper reference in memory map
    impl_->memoryMap.setMapper(&impl_->mapper);
    impl_->memoryMap.setCartridge(&impl_->cartridge_);
    impl_->memoryMap.setWriteObserver([this](uint16_t address, uint8_t value) {
        const auto observedValue = static_cast<uint8_t>(
            ((address >= 0xFF00u && address < 0xFF80u) || address == 0xFFFFu)
                ? impl_->memoryMap.read(address)
                : value);
        impl_->cpu.cpu().invalidateBlockCacheForWrite(address);
        impl_->cpu.cpu().syncCachedIoRegisterWrite(address, observedValue);
        if (address == 0xFF00u) {
            impl_->input.writeRegister(value);
            impl_->memoryMap.setIoRegisterRaw(0xFF00u, impl_->input.readRegister());
            impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFF00u, impl_->input.readRegister());
        }
        if ((address >= 0xFF10u && address <= 0xFF26u) ||
            (address >= 0xFF30u && address <= 0xFF3Fu)) {
            impl_->apu.writeRegister(address, value);
            impl_->memoryMap.setIoRegisterRaw(0xFF26u, impl_->apu.readRegister(0xFF26u));
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::MemoryWriteObserved,
                    BMMQ::PluginCategory::Audio,
                    impl_->stepCounter,
                    address,
                    value,
                    nullptr,
                    "audio register write"
                });
            }
        }
        if (address == 0xFF50u && value != 0) {
            impl_->bootEntryPending = true;
        }
        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {
            impl_->cartridge_.write(address, value);
        }
        if (address == 0xFF40u) {
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::MemoryWriteObserved,
                    BMMQ::PluginCategory::Video,
                    impl_->stepCounter,
                    address,
                    value,
                    nullptr,
                    "video memory write"
                });
            }
        }
        if (address == 0xFF01u || address == 0xFF02u) {
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::SerialActivity,
                    BMMQ::PluginCategory::Serial,
                    impl_->stepCounter,
                    address,
                    value,
                    nullptr,
                    "serial register write"
                });
            }
        }
    });

    // Create runtime context
    impl_->context = std::make_unique<GameBoyRuntimeContext>(
        impl_->cpu,
        impl_->memoryMap,
        impl_->romLoaded,
        impl_->activePolicy);

    // Configure audio engine
    (void)audioService().configureEngine({
        .sourceSampleRate = 48000,
        .deviceSampleRate = 48000,
        .channelCount = 1u,
        .ringBufferCapacitySamples = 2048u,
        .frameChunkSamples = 256u,
    });

    // Leave memory map using its fallback cartridge until a ROM is loaded
    impl_->input.reset();
    impl_->ppu.reset();
    impl_->apu.reset();
    impl_->cpu.cpu().resetDivider();
    impl_->cpu.cpu().setJoypadState(0x00u);

    BMMQ::Plugin::validateExecutorPolicyStartup(impl_->defaultPolicy);
}

GameBoyMachine::~GameBoyMachine() {
    (void)flushCartridgeSave();
    if (impl_->pluginManager.initialized()) {
        impl_->pluginManager.shutdown(view());
    }
}

void GameBoyMachine::loadRom(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        throw std::runtime_error("Cannot load empty ROM");
    }
    if (bytes.size() > kMaxRomSize) {
        throw std::runtime_error("ROM too large");
    }

    (void)flushCartridgeSave();

    // Load into cartridge (for save management)
    impl_->cartridge_.load(bytes);

    // Create mapper from ROM data
    impl_->mapper.load(bytes);

    // Bind save path if applicable
    if (romPathAllowsSaveBinding(impl_->pendingRomSourcePath)) {
        impl_->saveManager.bindRomPath(*impl_->pendingRomSourcePath);
        impl_->saveManager.load(impl_->cartridge_);
    } else {
        impl_->saveManager.clearBinding();
    }
    impl_->pendingRomSourcePath.reset();

    // Reset subsystems
    impl_->memoryMap.reset();
    impl_->ppu.reset();
    impl_->apu.reset();
    impl_->input.reset();
    impl_->cpu.cpu().resetDivider();
    impl_->cpu.cpu().setJoypadState(0x00u);

    impl_->romLoaded = true;
    ++impl_->inputGeneration;
    inputService().advanceGeneration(impl_->inputGeneration);
    impl_->lastDigitalInputMask.reset();
    impl_->stepCounter = 0u;
    impl_->lastAudioFrameCounter = 0u;
    impl_->bootEntryPending = false;

    std::array<uint8_t, 0x4000> fixedWindow{};
    impl_->mapper.copyRomBankWindow(0, fixedWindow);
    impl_->memoryMap.installRomWindow(0x0000u, fixedWindow);
    std::array<uint8_t, 0x4000> switchableWindow{};
    impl_->mapper.copyRomBankWindow(impl_->mapper.currentRomBank(), switchableWindow);
    impl_->memoryMap.installRomWindow(0x4000u, switchableWindow);

    // Initialize DMG startup registers
    auto& core = impl_->cpu.cpu();
    core.invalidateAllBlockCache();
    core.setIme(false);
    core.setStopFlag(false);
    core.clearHaltFlag();
    core.resetDivider();
    core.setJoypadState(0x00u);

    // Set CPU registers per Pan Docs
    impl_->context->writeRegister16(GB::RegisterId::AF, 0x01B0);
    impl_->context->writeRegister16(GB::RegisterId::BC, 0x0013);
    impl_->context->writeRegister16(GB::RegisterId::DE, 0x00D8);
    impl_->context->writeRegister16(GB::RegisterId::HL, 0x014D);
    impl_->context->writeRegister16(GB::RegisterId::SP, 0xFFFE);
    impl_->context->writeRegister16(GB::RegisterId::PC, 0x0100);

    impl_->context->write8(0xFF00, 0xCF);
    impl_->memoryMap.setIoRegisterRaw(0xFF50u, 0x01u);

    // Seed I/O register defaults
    static constexpr std::array<std::pair<std::string_view, uint8_t>, 37> kPostBootIoDefaults{{
        {"SB", 0x00}, {"SC", 0x7E}, {"TIMA", 0x00}, {"TMA", 0x00}, {"TAC", 0xF8},
        {"IF", 0xE1}, {"NR10", 0x80}, {"NR11", 0xBF}, {"NR12", 0xF3}, {"NR14", 0xBF},
        {"NR21", 0x3F}, {"NR22", 0x00}, {"NR24", 0xBF}, {"NR30", 0x7F}, {"NR31", 0xFF},
        {"NR32", 0x9F}, {"NR34", 0xBF}, {"NR41", 0xFF}, {"NR42", 0x00}, {"NR43", 0x00},
        {"NR44", 0xBF}, {"NR50", 0x77}, {"NR51", 0xF3}, {"NR52", 0xF1},
        {"LCDC", 0x91}, {"STAT", 0x85}, {"SCY", 0x00}, {"SCX", 0x00},
        {"LY", 0x00}, {"LYC", 0x00}, {"DMA", 0xFF},
        {"BGP", 0xFC}, {"OBP0", 0xFF}, {"OBP1", 0xFF},
        {"WY", 0x00}, {"WX", 0x00}, {"IE", 0x00},
    }};

    for (const auto& [name, value] : kPostBootIoDefaults) {
        auto* entry = impl_->cpu.cpu().getMemory().file.findRegister(name);
        if (entry != nullptr && entry->reg != nullptr) {
            entry->reg->value = value;
        }
        auto* desc = impl_->cpu.cpu().getMemory().file.findDescriptor(name);
        if (desc != nullptr && desc->mappedAddress.has_value()) {
            impl_->memoryMap.write(*desc->mappedAddress, value);
        }
    }

    core.resetApu();
    impl_->lastAudioFrameCounter = core.audioFrameCounter();

    // Emit RomLoaded event
    if (impl_->pluginManager.initialized()) {
        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
            BMMQ::MachineEventType::RomLoaded,
            BMMQ::PluginCategory::System,
            0u,
            0u,
            0u,
            nullptr,
            "ROM loaded"
        });
    }
}

void GameBoyMachine::loadRomFromPath(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open ROM: " + path.string());
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        throw std::runtime_error("ROM is empty: " + path.string());
    }

    setRomSourcePath(path);
    loadRom(bytes);
}

void GameBoyMachine::loadExternalBootRom(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != 0x100u) {
        throw std::invalid_argument("Game Boy boot ROM must be exactly 256 bytes");
    }
    impl_->memoryMap.mapBootRom(bytes.data(), bytes.size());
    impl_->memoryMap.setIoRegisterRaw(0xFF50u, 0x00u);
    impl_->context->writeRegister16(GB::RegisterId::PC, 0x0000u);
    impl_->cpu.cpu().invalidateAllBlockCache();
}

void GameBoyMachine::setRomSourcePath(const std::optional<std::filesystem::path>& path) {
    impl_->pendingRomSourcePath = path;
}

BMMQ::RuntimeContext& GameBoyMachine::runtimeContext() {
    return *impl_->context;
}

const BMMQ::RuntimeContext& GameBoyMachine::runtimeContext() const {
    return *impl_->context;
}

BMMQ::PluginManager& GameBoyMachine::pluginManager() {
    return impl_->pluginManager;
}

const BMMQ::PluginManager& GameBoyMachine::pluginManager() const {
    return impl_->pluginManager;
}

std::span<const BMMQ::IoRegionDescriptor> GameBoyMachine::describeIoRegions() const {
    return kIoRegions;
}

void GameBoyMachine::attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin& policy) {
    BMMQ::Plugin::validateExecutorPolicyStartup(policy);
    impl_->activePolicy = &policy;
    impl_->context->refreshExecutionMode();
}

const BMMQ::Plugin::IExecutorPolicyPlugin& GameBoyMachine::attachedExecutorPolicy() const {
    return *impl_->activePolicy;
}

BMMQ::ExecutionSliceResult GameBoyMachine::runSlice(const BMMQ::ExecutionBudget& budget) {
    if (impl_->bootEntryPending) {
        impl_->bootEntryPending = false;
        impl_->context->writeRegister16(GB::RegisterId::PC, 0x0100u);
        BMMQ::ExecutionSliceResult result;
        result.exitReason = BMMQ::ExecutionSliceExitReason::MachineBoundary;
        return result;
    }
    return BMMQ::Machine::runSlice(budget);
}

void GameBoyMachine::step() {
    (void)runSlice(BMMQ::ExecutionBudget{});
}

BMMQ::InstructionRetirementDecision GameBoyMachine::onInstructionRetired(
    const BMMQ::CpuFeedback& feedback,
    const BMMQ::ExecutionSliceProgress&)
{
    ++impl_->stepCounter;

    // Advance PPU by retired cycles
    impl_->ppu.step(feedback.retiredCycles);
    impl_->memoryMap.setIoRegisterRaw(0xFF44u, impl_->ppu.ly());
    impl_->memoryMap.setIoRegisterRaw(0xFF41u, static_cast<uint8_t>(
        (impl_->context->read8(0xFF41u) & 0xF8u) | (impl_->ppu.currentMode() & 0x03u)));

    // Advance APU by retired cycles
    impl_->apu.step(feedback.retiredCycles);
    impl_->memoryMap.setIoRegisterRaw(0xFF26u, impl_->apu.readRegister(0xFF26u));

    // Check PPU scanline ready events
    if (impl_->ppu.takeScanlineReady()) {
        auto ly = impl_->ppu.lastReadyScanline();
        if (impl_->pluginManager.initialized()) {
            impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                BMMQ::MachineEventType::VideoScanlineReady,
                BMMQ::PluginCategory::Video,
                impl_->stepCounter,
                0xFF44u,
                ly,
                &feedback,
                "visible scanline ready"
            });
        }
    }

    // Check VBlank events
    if (impl_->ppu.takeVBlankEntered()) {
        if (impl_->pluginManager.initialized()) {
            impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                BMMQ::MachineEventType::VBlank,
                BMMQ::PluginCategory::Video,
                impl_->stepCounter,
                0xFF44u,
                static_cast<uint8_t>(144u),
                &feedback,
                "entered VBlank"
            });
        }
        // Flush cartridge save on VBlank
        if (impl_->backgroundTaskService == nullptr) {
            (void)flushCartridgeSave();
        } else {
            if (impl_->pendingSaveSnapshot.has_value()) {
                impl_->pendingSaveSnapshot = flushSaveSnapshotViaBackground(
                    *impl_->backgroundTaskService, std::move(*impl_->pendingSaveSnapshot));
            }
            if (!impl_->pendingSaveSnapshot.has_value()) {
                auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);
                if (extracted.has_value()) {
                    impl_->pendingSaveSnapshot = flushSaveSnapshotViaBackground(
                        *impl_->backgroundTaskService, std::move(*extracted));
                }
            }
        }
    }

    // Check audio frame counter changes
    const auto audioFrameCounter = impl_->apu.frameCounter();
    if (audioFrameCounter != impl_->lastAudioFrameCounter) {
        impl_->lastAudioFrameCounter = audioFrameCounter;
        if (impl_->pluginManager.initialized()) {
            impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                BMMQ::MachineEventType::AudioFrameReady,
                BMMQ::PluginCategory::Audio,
                impl_->stepCounter,
                0xFF26u,
                impl_->context->read8(0xFF26u),
                &feedback,
                "apu frame mixed"
            });
        }
    }

    // Periodic save flush
    if ((impl_->stepCounter % 4096u) == 0u) {
        if (impl_->backgroundTaskService != nullptr) {
            if (impl_->pendingSaveSnapshot.has_value()) {
                impl_->pendingSaveSnapshot = flushSaveSnapshotViaBackground(
                    *impl_->backgroundTaskService, std::move(*impl_->pendingSaveSnapshot));
            }
            if (!impl_->pendingSaveSnapshot.has_value()) {
                auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);
                if (extracted.has_value()) {
                    impl_->pendingSaveSnapshot = flushSaveSnapshotViaBackground(
                        *impl_->backgroundTaskService, std::move(*extracted));
                }
            }
        }
    }

    // Conservatively leave a multi-instruction slice whenever a device-visible
    // interrupt is pending. The next CPU entry owns the exact IME/HALT decision.
    const auto pendingInterrupts = static_cast<uint8_t>(
        impl_->context->read8(0xFF0Fu) & impl_->context->read8(0xFFFFu) & 0x1Fu);
    if (pendingInterrupts != 0u) {
        return BMMQ::InstructionRetirementDecision::exitSlice(
            BMMQ::ExecutionSliceExitReason::MachineBoundary);
    }
    return BMMQ::InstructionRetirementDecision::continueSlice();
}

void GameBoyMachine::serviceInput() {
    if (inputService().state() == BMMQ::InputLifecycleState::Active) {
        (void)inputService().pollActiveAdapter(impl_->inputGeneration);
        if (const auto committedInput = inputService().committedDigitalMask(); committedInput.has_value()) {
            const auto pressedMask = static_cast<uint8_t>(*committedInput & 0x00FFu);
            impl_->input.setLogicalButtons(pressedMask);
            impl_->lastDigitalInputMask = pressedMask;
            const auto joypad = impl_->input.readRegister();
            impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFF00u, joypad);
            impl_->memoryMap.setIoRegisterRaw(0xFF00u, joypad);
            if (impl_->pluginManager.initialized()) {
                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
                    BMMQ::MachineEventType::DigitalInputChanged,
                    BMMQ::PluginCategory::DigitalInput,
                    impl_->stepCounter,
                    0xFF00u,
                    joypad,
                    nullptr,
                    "input service sample"
                });
            }
            return;
        }
    }

    if (!impl_->pluginManager.initialized()) {
        return;
    }

    if (const auto sampledInput = impl_->pluginManager.sampleDigitalInput(view()); sampledInput.has_value()) {
        const auto pressedMask = static_cast<uint8_t>(*sampledInput & 0x00FFu);
        impl_->input.setLogicalButtons(pressedMask);
        impl_->lastDigitalInputMask = pressedMask;
        const auto joypad = impl_->input.readRegister();
        impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFF00u, joypad);
        impl_->memoryMap.setIoRegisterRaw(0xFF00u, joypad);
        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
            BMMQ::MachineEventType::DigitalInputChanged,
            BMMQ::PluginCategory::DigitalInput,
            impl_->stepCounter,
            0xFF00u,
            joypad,
            nullptr,
            "plugin input sample"
        });
    }
}

std::optional<uint32_t> GameBoyMachine::currentDigitalInputMask() const {
    if (!impl_->lastDigitalInputMask.has_value()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(*impl_->lastDigitalInputMask);
}

std::vector<int16_t> GameBoyMachine::recentAudioSamples() const {
    return impl_->apu.copyRecentSamples();
}

uint32_t GameBoyMachine::audioSampleRate() const {
    return impl_->apu.sampleRate();
}

uint8_t GameBoyMachine::audioChannelCount() const {
    return 1u;
}

uint64_t GameBoyMachine::audioFrameCounter() const {
    return impl_->apu.frameCounter();
}

std::string_view GameBoyMachine::visualTargetId() const noexcept {
    return "gameboy";
}

const BMMQ::IVisualDebugAdapter* GameBoyMachine::visualDebugAdapter() const noexcept {
    return &gameBoyVisualDebugAdapter();
}

std::optional<BMMQ::VideoDebugFrameModel> GameBoyMachine::videoDebugFrameModel(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    return gameBoyVisualDebugAdapter().buildFrameModel(*this, request);
}

std::optional<BMMQ::RealtimeVideoSubmission> GameBoyMachine::realtimeVideoPacket(
    const BMMQ::VideoDebugRenderRequest& request) const
{
    return impl_->ppu.buildRealtimeFrame(request);
}

std::optional<BMMQ::VideoStateView> GameBoyMachine::videoStateSnapshot() const
{
    BMMQ::VideoStateView state;
    for (const auto& region : describeIoRegions()) {
        if (region.category != BMMQ::PluginCategory::Video) {
            continue;
        }
        if (region.label == "VRAM") {
            state.vramRegion = region;
        } else if (region.label == "OAM") {
            state.oamRegion = region;
        } else if (region.label == "LCD Registers") {
            state.registerRegion = region;
        }
    }
    if (state.vramRegion.size == 0u || state.oamRegion.size == 0u || state.registerRegion.size == 0u) {
        return std::nullopt;
    }

    const auto vram = impl_->memoryMap.vramSpan();
    const auto oam = impl_->memoryMap.oamSpan();
    state.vram.assign(vram.begin(), vram.end());
    state.oam.assign(oam.begin(), oam.end());
    state.lcdc = impl_->memoryMap.read(0xFF40u);
    state.stat = impl_->memoryMap.read(0xFF41u);
    state.scy = impl_->memoryMap.read(0xFF42u);
    state.scx = impl_->memoryMap.read(0xFF43u);
    state.ly = impl_->memoryMap.read(0xFF44u);
    state.lyc = impl_->memoryMap.read(0xFF45u);
    state.bgp = impl_->memoryMap.read(0xFF47u);
    state.obp0 = impl_->memoryMap.read(0xFF48u);
    state.obp1 = impl_->memoryMap.read(0xFF49u);
    state.wy = impl_->memoryMap.read(0xFF4Au);
    state.wx = impl_->memoryMap.read(0xFF4Bu);
    return state;
}

std::optional<BMMQ::RealtimeAudioPacket> GameBoyMachine::realtimeAudioPacket() const {
    BMMQ::RealtimeAudioPacket packet;
    packet.sampleRate = impl_->apu.sampleRate();
    packet.channelCount = 1u;
    packet.frameCounter = impl_->apu.frameCounter();
    packet.pcmSamples = impl_->apu.takePendingSamples();
    return packet;
}

bool GameBoyMachine::flushCartridgeSave() {
    if (!impl_->cartridge_.supportsBatterySave()) return false;
    auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);
    if (!extracted.has_value()) return false;
    GB::CartridgeSaveManager::flushSnapshot(std::move(*extracted));
    return true;
}

void GameBoyMachine::flushPendingBackgroundWork()
{
    if (impl_->pendingSaveSnapshot.has_value()) {
        CartridgeSaveManager::flushSnapshot(*impl_->pendingSaveSnapshot);
        impl_->pendingSaveSnapshot.reset();
    }
    (void)flushCartridgeSave();
}

uint16_t GameBoyMachine::readRegisterPair(std::string_view id) const {
    return impl_->context->readRegister16(id);
}

std::string GameBoyMachine::stopSummary() const {
    const auto pc = impl_->context->readRegister16(GB::RegisterId::PC);
    const auto ly = impl_->context->read8(0xFF44);
    const auto lcdc = impl_->context->read8(0xFF40);
    const auto stat = impl_->context->read8(0xFF41);
    const auto interruptFlags = impl_->context->read8(0xFF0F);
    const auto interruptEnable = impl_->context->read8(0xFFFF);

    std::ostringstream out;
    out << "PC=0x" << std::hex << std::uppercase << pc << std::dec << '\n'
        << "I/O state: LY=0x" << std::hex << std::uppercase << static_cast<int>(ly)
        << " LCDC=0x" << static_cast<int>(lcdc)
        << " STAT=0x" << static_cast<int>(stat)
        << " IF=0x" << static_cast<int>(interruptFlags)
        << " IE=0x" << static_cast<int>(interruptEnable)
        << std::dec;
    return out.str();
}

void GameBoyMachine::setJoypadState(uint8_t value) {
    impl_->cpu.cpu().setJoypadState(value);
    impl_->input.setLogicalButtons(value);
    impl_->lastDigitalInputMask = value;
    impl_->memoryMap.write(0xFF00u, impl_->input.readRegister());

    if (impl_->pluginManager.initialized()) {
        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{
            BMMQ::MachineEventType::DigitalInputChanged,
            BMMQ::PluginCategory::DigitalInput,
            impl_->stepCounter,
            0xFF00u,
            impl_->input.readRegister(),
            nullptr,
            "joypad state changed"
        });
    }
}

GameBoyMachine::BlockCacheStats GameBoyMachine::blockCacheStats() const {
    const auto stats = impl_->cpu.cpu().blockCacheStats();
    return BlockCacheStats{
        stats.hits,
        stats.misses,
        stats.invalidations,
        stats.translations,
        stats.translatedInstructions,
        stats.guardFailures,
        stats.chainContinuations,
        stats.unsupportedFallbacks
    };
}

bool GameBoyMachine::blockCacheEnabled() const {
    return impl_->cpu.cpu().blockCacheEnabled();
}

void GameBoyMachine::setBlockCacheEnabled(bool enabled) {
    impl_->cpu.cpu().setBlockCacheEnabled(enabled);
}

void GameBoyMachine::save_state(const std::filesystem::path& path) {
    if (!impl_->romLoaded) {
        throw std::runtime_error("Cannot save Game Boy state before ROM is loaded");
    }

    StateWriter machineWriter;
    machineWriter.u64(impl_->stepCounter);
    machineWriter.u64(impl_->lastAudioFrameCounter);
    machineWriter.boolean(impl_->bootEntryPending);
    machineWriter.boolean(impl_->interruptRequested);
    machineWriter.boolean(impl_->lastDigitalInputMask.has_value());
    if (impl_->lastDigitalInputMask.has_value()) {
        machineWriter.u32(*impl_->lastDigitalInputMask);
    }
    machineWriter.u64(impl_->inputGeneration);

    BMMQ::SaveStateFile state;
    state.header.core_id = BMMQ::kCoreId_GameBoy;
    state.header.checksum = BMMQ::SaveStateChecksum::Crc32;
    state.header.rom_hash = BMMQ::crc32(
        impl_->mapper.romData().data(),
        impl_->mapper.romData().size());
    state.chunks.push_back(makeChunk("gb.machine", machineWriter.take()));
    state.chunks.push_back(makeChunk("gb.cpu", serializeCpuState(impl_->cpu.cpu().exportState())));
    state.chunks.push_back(makeChunk("gb.memory", impl_->memoryMap.exportState()));
    state.chunks.push_back(makeChunk("gb.ppu", impl_->ppu.exportState()));
    state.chunks.push_back(makeChunk("gb.apu", serializeApuState(impl_->apu.exportState())));
    state.chunks.push_back(makeChunk("gb.input", impl_->input.exportState()));
    state.chunks.push_back(makeChunk("gb.mapper", serializeMapperState(impl_->mapper.exportState())));
    state.chunks.push_back(makeChunk("gb.cartridge", serializeCartridgeState(impl_->cartridge_.exportState())));
    BMMQ::SaveStateReader::write(state, path);
}

void GameBoyMachine::load_state(const std::filesystem::path& path) {
    if (!impl_->romLoaded) {
        throw std::runtime_error("Load ROM before loading Game Boy save state");
    }

    const auto state = BMMQ::SaveStateReader::readForCore(
        path,
        BMMQ::kCoreId_GameBoy,
        impl_->mapper.romData());

    const auto mapperData = requireChunk(state, "gb.mapper").data;
    const auto cartridgeData = requireChunk(state, "gb.cartridge").data;
    const auto memoryData = requireChunk(state, "gb.memory").data;
    const auto ppuData = requireChunk(state, "gb.ppu").data;
    const auto apuData = requireChunk(state, "gb.apu").data;
    const auto inputData = requireChunk(state, "gb.input").data;
    const auto cpuData = requireChunk(state, "gb.cpu").data;

    StateReader machineReader(requireChunk(state, "gb.machine").data);
    const uint64_t stepCounter = machineReader.u64();
    const uint64_t lastAudioFrameCounter = machineReader.u64();
    const bool bootEntryPending = machineReader.boolean();
    const bool interruptRequested = machineReader.boolean();

    std::optional<uint8_t> lastDigitalInputMask{};
    if (machineReader.boolean()) {
        const uint32_t mask = machineReader.u32();
        if (mask <= 0xFFu) {
            lastDigitalInputMask = static_cast<uint8_t>(mask);
        }
    }
    const uint64_t inputGeneration = machineReader.u64();
    if (!machineReader.done()) {
        throw std::invalid_argument("Game Boy machine save state has trailing data");
    }

    // Stage all imports into temporaries so a failure leaves impl_ unchanged.
    GameBoyMapper::SaveState mapperState = deserializeMapperState(mapperData);
    GameBoyCartridge::State cartridgeState = deserializeCartridgeState(cartridgeData);
    GB::GameBoyCartridge nextCartridge = impl_->cartridge_;
    nextCartridge.importState(cartridgeState);

    GB::GameBoyMapper nextMapper = impl_->mapper;
    std::vector<uint8_t> romData(impl_->mapper.romData().begin(), impl_->mapper.romData().end());
    nextMapper.load(romData);
    nextMapper.importState(mapperState);

    GB::GameBoyAPU nextApu = impl_->apu;
    nextApu.importState(deserializeApuState(apuData));

    GB::GameBoyPPU nextPpu = impl_->ppu;
    nextPpu.importState(ppuData);

    GB::GameBoyInput nextInput = impl_->input;
    nextInput.importState(inputData);

    LR3592_DMG::SaveState cpuState = deserializeCpuState(cpuData);
    GameBoyMemoryMap nextMemoryMap;
    nextMemoryMap.importState(memoryData);

    // Preserve the existing memory write observer across the memory-map replacement.
    const auto preservedWriteObserver = impl_->memoryMap.writeObserver();

    // Commit staged state into the live machine only after all imports succeeded.
    impl_->mapper = std::move(nextMapper);
    impl_->cartridge_ = std::move(nextCartridge);
    impl_->memoryMap = std::move(nextMemoryMap);
    impl_->memoryMap.setWriteObserver(std::move(preservedWriteObserver));
    impl_->ppu = std::move(nextPpu);
    impl_->apu = std::move(nextApu);
    impl_->input = std::move(nextInput);
    impl_->cpu.cpu().importState(cpuState);

    impl_->stepCounter = stepCounter;
    impl_->lastAudioFrameCounter = lastAudioFrameCounter;
    impl_->bootEntryPending = bootEntryPending;
    impl_->interruptRequested = interruptRequested;
    impl_->lastDigitalInputMask = lastDigitalInputMask;
    impl_->inputGeneration = inputGeneration;

    impl_->memoryMap.setMapper(&impl_->mapper);
    impl_->memoryMap.setCartridge(&impl_->cartridge_);
    impl_->ppu.memoryMap = &impl_->memoryMap;
    impl_->input.writeRegister(impl_->memoryMap.read(0xFF00u));
    impl_->memoryMap.setIoRegisterRaw(0xFF00u, impl_->input.readRegister());
    for (uint16_t address = 0xFF00u; address < 0xFF80u; ++address) {
        impl_->cpu.cpu().syncCachedIoRegisterWrite(address, impl_->memoryMap.read(address));
    }
    impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFFFFu, impl_->memoryMap.read(0xFFFFu));
    impl_->cpu.cpu().syncCachedIoRegisterWrite(0xFF26u, impl_->apu.readRegister(0xFF26u));
    inputService().advanceGeneration(impl_->inputGeneration);
}

} // namespace GB
