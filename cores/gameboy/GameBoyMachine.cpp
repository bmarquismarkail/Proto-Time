#include \"GameBoyMachine.hpp\"\
#include <algorithm>\
#include <array>\
#include <cctype>\
#include <cstdio>\
#include <filesystem>\
#include <fstream>\
#include <optional>\
#include <span>\
#include <sstream>\
#include <stdexcept>\
#include <string_view>\
\n\
#include \"../../inst_cycle/executor/PluginContract.hpp\"\
#include \"../../machine/plugins/IoPlugin.hpp\"\
#include \"../../machine/plugins/PluginManager.hpp\"\
#include \"../../machine/BackgroundTaskService.hpp\"\
#include \"cartridge/CartridgeSaveManager.hpp\"\
#include \"hardware_registers.hpp\"\
#include \"register_id.hpp\"\
#include \"video/GameBoyVisualDebugAdapter.hpp\"\
\n\
namespace GB {\n\n// Expose Game Boy memory-mapped I/O descriptors.\
static constexpr std::size_t kMaxRomSize = 1024u * 1024u;\n\nconstexpr std::array<BMMQ::IoRegionDescriptor, 7> kIoRegions{{\n    {BMMQ::PluginCategory::Video, 0x8000u, 0x2000u, \"VRAM\", true, true},\
    {BMMQ::PluginCategory::Video, 0xFE00u, 0x00A0u, \"OAM\", true, true},\
    {BMMQ::PluginCategory::Video, 0xFF40u, 0x000Cu, \"LCD Registers\", true, true},\
    {BMMQ::PluginCategory::Audio, 0xFF10u, 0x0017u, \"APU Registers\", true, true},\
    {BMMQ::PluginCategory::Audio, 0xFF30u, 0x0010u, \"Wave RAM\", true, true},\
    {BMMQ::PluginCategory::DigitalInput, 0xFF00u, 0x0001u, \"Joypad\", true, true},\
    {BMMQ::PluginCategory::Serial, 0xFF01u, 0x0002u, \"Serial Registers\", true, true},\
}};\n\n[[nodiscard]] inline bool romPathAllowsSaveBinding(const std::optional<std::filesystem::path>& path) {\n    if (!path.has_value()) return false;\n    auto extension = path->extension().string();\
    std::transform(extension.begin(), extension.end(), extension.begin(),\
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });\
    return extension != \".sms\"; // Game Boy ROMs allow saves, SMS typically doesn't\n}\n\ninline void flushSaveSnapshotViaBackground(\
    BMMQ::BackgroundTaskService& backgroundTaskService,\
    CartridgeSaveManager::SaveSnapshot snapshot)\
{\n    auto queuedSnapshot = snapshot;\n    const bool queued = backgroundTaskService.submit([snapshot = std::move(queuedSnapshot)]() {\n        CartridgeSaveManager::flushSnapshot(snapshot);\
    });\
    if (!queued) {\n        CartridgeSaveManager::flushSnapshot(snapshot);\
    }\n}\n\n// Game Boy runtime context — mirrors GameGearRuntimeContext pattern.\
class GameBoyRuntimeContext final : public BMMQ::RuntimeContext {\npublic:\n    GameBoyRuntimeContext(LR3592_PluginRuntime& runtime,\
                          GameBoyMemoryMap& memoryMap,\
                          bool& romLoaded,\
                          BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy)\
        : runtime_(runtime),\
          memoryMap_(memoryMap),\
          romLoaded_(romLoaded),\
          activePolicy_(activePolicy) {\n        refreshExecutionMode();\
    }\n\n    FetchBlock fetch() override {\n        if (!romLoaded_) {\n            throw std::runtime_error(\"ROM is not loaded\");\
        }\n        return runtime_.fetch();\
    }\n\n    ExecutionBlock decode(FetchBlock& fetchBlock) override {\n        return runtime_.decode(fetchBlock);\
    }\n\n    void execute(const ExecutionBlock& block, FetchBlock& fetchBlock) override {\n        runtime_.execute(block, fetchBlock);\
    }\n\n    BMMQ::CpuFeedback step(FetchBlock& fetchBlock) override {\n        if (fastExecutionAllowed() && runtime_.cpu().tryFastExecute(fetchBlock)) {\n            return runtime_.getLastFeedback();\
        }\n        cachedExecutionBlock_.clear();\
        cachedExecutionBlock_.reserve(4);\n        runtime_.cpu().decodeInto(fetchBlock, cachedExecutionBlock_);\
        runtime_.execute(cachedExecutionBlock_, fetchBlock);\
        return runtime_.getLastFeedback();\
    }\n\n    BMMQ::CpuFeedback step() override {\n        if (!romLoaded_) {\n            throw std::runtime_error(\"ROM is not loaded\");\
        }\n        runtime_.cpu().fetchInto(cachedFetchBlock_);\
        return step(cachedFetchBlock_);\
    }\n\n    uint8_t read8(uint16_t address) const override {\n        return memoryMap_.read(resolveEchoAddress(address));\
    }\n\n    uint8_t peek8(uint16_t address) const override {\n        address = resolveEchoAddress(address);\
        // For addresses that go through cartridge intercept, use read8\
        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {\n            return read8(address);\
        }\n        return memoryMap_.read(address);\
    }\n\n    void write8(uint16_t address, uint8_t value) override {\n        memoryMap_.write(resolveEchoAddress(address), value);\
    }\n\n    uint8_t readRegister8(std::string_view id) const override {\n        const auto& descriptor = requireDescriptor(id);\
        if (descriptor.width != BMMQ::RegisterWidth::Byte8) {\n            throw std::invalid_argument(\"register width mismatch\");\
        }\n        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {\n            if (!descriptor.mappedAddress.has_value()) {\n                throw std::invalid_argument(\"address-backed register missing address\");\
            }\n            return read8(*descriptor.mappedAddress);\
        }\n        auto* entry = requireRegisterEntry(id);\
        return static_cast<uint8_t>(entry->reg->value & 0x00FFu);\
    }\n\n    void writeRegister8(std::string_view id, uint8_t value) override {\n        const auto& descriptor = requireDescriptor(id);\
        if (descriptor.width != BMMQ::RegisterWidth::Byte8) {\n            throw std::invalid_argument(\"register width mismatch\");\
        }\
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {\n            if (!descriptor.mappedAddress.has_value()) {\n                throw std::invalid_argument(\"address-backed register missing address\");\
            }\
            write8(*descriptor.mappedAddress, value);\
            return;\
        }\
        auto* entry = requireRegisterEntry(id);\
        entry->reg->value = value;\
    }\n\n    uint16_t readRegister16(std::string_view id) const override {\n        const auto& descriptor = requireDescriptor(id);\
        if (descriptor.width != BMMQ::RegisterWidth::Word16) {\n            throw std::invalid_argument(\"register width mismatch\");\
        }\
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {\n            if (!descriptor.mappedAddress.has_value()) {\n                throw std::invalid_argument(\"address-backed register missing address\");\
            }\
            return read16(*descriptor.mappedAddress);\
        }\
        auto* entry = requireRegisterEntry(id);\
        return entry->reg->value;\
    }\n\n    void writeRegister16(std::string_view id, uint16_t value) override {\n        const auto& descriptor = requireDescriptor(id);\
        if (descriptor.width != BMMQ::RegisterWidth::Word16) {\n            throw std::invalid_argument(\"register width mismatch\");\
        }\
        if (descriptor.storage == BMMQ::RegisterStorage::AddressMapped) {\n            if (!descriptor.mappedAddress.has_value()) {\n                throw std::invalid_argument(\"address-backed register missing address\");\
            }\
            write16(*descriptor.mappedAddress, value);\
            return;\
        }\
        auto* entry = requireRegisterEntry(id);\
        entry->reg->value = value;\
    }\n\n    uint16_t readRegisterPair(std::string_view id) const override {\n        const auto& descriptor = requireDescriptor(id);\
        if (!descriptor.isPair) {\n            throw std::invalid_argument(\"register is not a pair\");\
        }\n        auto* entry = requireRegisterEntry(id);\
        return entry->reg->value;\
    }\n\n    void writeRegisterPair(std::string_view id, uint16_t value) override {\n        const auto& descriptor = requireDescriptor(id);\
        if (!descriptor.isPair) {\n            throw std::invalid_argument(\"register is not a pair\");\
        }\
        auto* entry = requireRegisterEntry(id);\
        entry->reg->value = value;\
    }\n\n    const BMMQ::CpuFeedback& getLastFeedback() const override {\n        return runtime_.getLastFeedback();\
    }\n\n    uint32_t clockHz() const override {\n        return runtime_.cpu().clockHz();\
    }\n\n    BMMQ::ExecutionGuarantee guarantee() const override {\n        return activePolicy_->guarantee();\
    }\n\n    const BMMQ::Plugin::PluginMetadata* attachedPolicyMetadata() const override {\n        return &activePolicy_->metadata();\
    }\n\n    const BMMQ::Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override {\n        return *activePolicy_;\
    }\n\n    void refreshExecutionMode() {\n        allowFastPath_ = activePolicy_ != nullptr &&\
            activePolicy_->guarantee() != BMMQ::ExecutionGuarantee::BaselineFaithful;\
    }\n\n    [[nodiscard]] bool fastExecutionAllowed() const noexcept {\n        return allowFastPath_;\
    }\n\nprivate:\n    static uint16_t resolveEchoAddress(uint16_t address) {\n        if (address >= 0xE000 && address <= 0xFDFF) {\n            return static_cast<uint16_t>(address - 0x2000);\
        }\
        return address;\
    }\n\n    using RegisterEntry = decltype(std::declval<BMMQ::RegisterFile<uint16_t>&>().findRegister(\"AF\"));\n\n    const BMMQ::RegisterDescriptor& requireDescriptor(std::string_view id) const {\n        const auto* descriptor = runtime_.cpu().getMemory().file.findDescriptor(id);\
        if (descriptor == nullptr) {\n            throw std::invalid_argument(\"register not found\");\
        }\n        return *descriptor;\
    }\n\n    RegisterEntry requireRegisterEntry(std::string_view id) const {\n        auto* entry = runtime_.cpu().getMemory().file.findRegister(id);\
        if (entry == nullptr || entry->reg == nullptr) {\n            throw std::invalid_argument(\"register not found\");\
        }\n        return entry;\
    }\n\n    LR3592_PluginRuntime& runtime_;\
    GameBoyMemoryMap& memoryMap_;\
    FetchBlock cachedFetchBlock_{};\
    ExecutionBlock cachedExecutionBlock_{};\
    const bool& romLoaded_;\
    BMMQ::Plugin::IExecutorPolicyPlugin*& activePolicy_;\
    bool allowFastPath_ = false;\
};\n\n\n// ---------------------------------------------------------------------------\
// GameBoyMachine implementation\
// ---------------------------------------------------------------------------\
GameBoyMachine::GameBoyMachine() : impl_(std::make_unique<Impl>()) {\n    videoService().setVisualDebugAdapter(visualDebugAdapter());\n\n    // Wire CPU memory interface to memory map\
    impl_->runtime.cpu().attachMemory(impl_->memoryMap);\n\n    // Wire PPU to memory map for VRAM/OAM reads\
    impl_->ppu.memoryMap = &impl_->memoryMap;\n\n    // Set up mapper reference in memory map\
    impl_->memoryMap.setMapper(&impl_->mapper);\
    impl_->memoryMap.setCartridge(&impl_->cartridge_);\
    impl_->memoryMap.setWriteObserver([this](uint16_t address, uint8_t value) {\n        impl_->runtime.cpu().syncCachedIoRegisterWrite(address, value);\
        if (address == 0xFF00u) {\n            impl_->input.writeRegister(value);\
            impl_->memoryMap.setIoRegisterRaw(0xFF00u, impl_->input.readRegister());\
        }\n        if ((address >= 0xFF10u && address <= 0xFF26u) ||\
            (address >= 0xFF30u && address <= 0xFF3Fu)) {\n            impl_->apu.writeRegister(address, value);\
            impl_->memoryMap.setIoRegisterRaw(0xFF26u, impl_->apu.readRegister(0xFF26u));\
            if (impl_->pluginManager.initialized()) {\n                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{\n                    BMMQ::MachineEventType::MemoryWriteObserved,\
                    BMMQ::PluginCategory::Audio,\
                    impl_->stepCounter,\
                    address,\
                    value,\
                    nullptr,\
                    \"audio register write\"\
                });\n            }\
        }\n        if (address == 0xFF50u && value != 0) {\n            impl_->bootEntryPending = true;\
        }\n        if (address < 0x8000u || (address >= 0xA000u && address < 0xC000u)) {\n            impl_->cartridge_.write(address, value);\
        }\n        if (address < 0x8000u) {\n            impl_->runtime.cpu().invalidateAllBlockCache();\
        }\n        if (address == 0xFF40u) {\n            if (impl_->pluginManager.initialized()) {\n                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{\n                    BMMQ::MachineEventType::MemoryWriteObserved,\
                    BMMQ::PluginCategory::Video,\
                    impl_->stepCounter,\
                    address,\
                    value,\
                    nullptr,\
                    \"video memory write\"\
                });\n            }\
        }\n        if (address == 0xFF01u || address == 0xFF02u) {\n            if (impl_->pluginManager.initialized()) {\n                impl_->pluginManager.emit(view(), BMMQ::MachineEvent{\n                    BMMQ::MachineEventType::SerialActivity,\
                    BMMQ::PluginCategory::Serial,\
                    impl_->stepCounter,\
                    address,\
                    value,\
                    nullptr,\
                    \"serial register write\"\
                });\n            }\
        }\n    });\n\n    // Create runtime context\
    impl_->context = std::make_unique<GameBoyRuntimeContext>(\n        impl_->runtime,\
        impl_->memoryMap,\
        impl_->romLoaded,\
        impl_->activePolicy);\n\n    // Configure audio engine\
    (void)audioService().configureEngine({\n        .sourceSampleRate = 48000,\
        .deviceSampleRate = 48000,\
        .channelCount = 1u,\
        .ringBufferCapacitySamples = 2048u,\
        .frameChunkSamples = 256u,\
    });\n\n    // Leave memory map using its fallback cartridge until a ROM is loaded\
    impl_->input.reset();\
    impl_->ppu.reset();\
    impl_->apu.reset();\
    impl_->runtime.cpu().resetDivider();\
    impl_->runtime.cpu().setJoypadState(0x00u);\n\n    BMMQ::Plugin::validateExecutorPolicyStartup(impl_->defaultPolicy);\n}\n\nGameBoyMachine::~GameBoyMachine() {\n    (void)flushCartridgeSave();\
    if (impl_->pluginManager.initialized()) {\n        impl_->pluginManager.shutdown(view());\
    }\n}\n\nvoid GameBoyMachine::loadRom(const std::vector<uint8_t>& bytes) {\n    if (bytes.empty()) {\n        throw std::runtime_error(\"Cannot load empty ROM\");\
    }\n    if (bytes.size() > kMaxRomSize) {\n        throw std::runtime_error(\"ROM too large\");\
    }\n\n    (void)flushCartridgeSave();\n\n    // Load into cartridge (for save management)\
    impl_->cartridge_.load(bytes);\n\n    // Create mapper from ROM data\
    impl_->mapper.load(bytes);\n\n    // Bind save path if applicable\
    if (romPathAllowsSaveBinding(impl_->pendingRomSourcePath)) {\n        impl_->saveManager.bindRomPath(*impl_->pendingRomSourcePath);\
        impl_->saveManager.load(impl_->cartridge_);\
    } else {\n        impl_->saveManager.clearBinding();\
    }\n    impl_->pendingRomSourcePath.reset();\n\n    // Reset subsystems\
    impl_->memoryMap.reset();\
    impl_->ppu.reset();\
    impl_->apu.reset();\
    impl_->input.reset();\
    impl_->runtime.cpu().resetDivider();\
    impl_->runtime.cpu().setJoypadState(0x00u);\n\n    impl_->romLoaded = true;\
    ++impl_->inputGeneration;\
    inputService().advanceGeneration(impl_->inputGeneration);\
    impl_->lastDigitalInputMask.reset();\
    impl_->stepCounter = 0u;\
    impl_->lastAudioFrameCounter = 0u;\
    impl_->bootEntryPending = false;\n\n    std::array<uint8_t, 0x4000> fixedWindow{};\
    impl_->mapper.copyRomBankWindow(0, fixedWindow);\
    impl_->memoryMap.installRomWindow(0x0000u, fixedWindow);\
    std::array<uint8_t, 0x4000> switchableWindow{};\
    impl_->mapper.copyRomBankWindow(impl_->mapper.currentRomBank(), switchableWindow);\
    impl_->memoryMap.installRomWindow(0x4000u, switchableWindow);\
\n    // Initialize DMG startup registers\
    auto& core = impl_->runtime.cpu();\
    core.setIme(false);\
    core.setStopFlag(false);\
    core.clearHaltFlag();\
    core.resetDivider();\
    core.setJoypadState(0x00u);\n\n    // Set CPU registers per Pan Docs\
    impl_->context->writeRegister16(GB::RegisterId::AF, 0x01B0);\
    impl_->context->writeRegister16(GB::RegisterId::BC, 0x0013);\
    impl_->context->writeRegister16(GB::RegisterId::DE, 0x00D8);\
    impl_->context->writeRegister16(GB::RegisterId::HL, 0x014D);\
    impl_->context->writeRegister16(GB::RegisterId::SP, 0xFFFE);\
    impl_->context->writeRegister16(GB::RegisterId::PC, 0x0100);\n\n    impl_->context->write8(0xFF00, 0xCF);\
    impl_->memoryMap.setIoRegisterRaw(0xFF50u, 0x01u);\n\n    // Seed I/O register defaults\
    static constexpr std::array<std::pair<std::string_view, uint8_t>, 37> kPostBootIoDefaults{{\n        {\"SB\", 0x00}, {\"SC\", 0x7E}, {\"TIMA\", 0x00}, {\"TMA\", 0x00}, {\"TAC\", 0xF8},\
        {\"IF\", 0xE1}, {\"NR10\", 0x80}, {\"NR11\", 0xBF}, {\"NR12\", 0xF3}, {\"NR14\", 0xBF},\
        {\"NR21\", 0x3F}, {\"NR22\", 0x00}, {\"NR24\", 0xBF}, {\"NR30\", 0x7F}, {\"NR31\", 0xFF},\
        {\"NR32\", 0x9F}, {\"NR34\", 0xBF}, {\"NR41\", 0xFF}, {\"NR42\", 0x00}, {\"NR43\", 0x00},\
        {\"NR44\", 0xBF}, {\"NR50\", 0x77}, {\"NR51\", 0xF3}, {\"NR52\", 0xF1},\
        {\"LCDC\", 0x91}, {\"STAT\", 0x85}, {\"SCY\", 0x00}, {\"SCX\", 0x00},\
        {\"LY\", 0x00}, {\"LYC\", 0x00}, {\"DMA\", 0xFF},\
        {\"BGP\", 0xFC}, {\"OBP0\", 0xFF}, {\"OBP1\", 0xFF},\
        {\"WY\", 0x00}, {\"WX\", 0x00}, {\"IE\", 0x00},\
    }};\n\n    for (const auto& [name, value] : kPostBootIoDefaults) {\n        auto* entry = impl_->runtime.cpu().getMemory().file.findRegister(name);\
        if (entry != nullptr && entry->reg != nullptr) {\n            entry->reg->value = value;\
        }\n        auto* desc = impl_->runtime.cpu().getMemory().file.findDescriptor(name);\
        if (desc != nullptr && desc->mappedAddress.has_value()) {\n            impl_->memoryMap.write(*desc->mappedAddress, value);\
        }\n    }\n\n    core.resetApu();\
    impl_->lastAudioFrameCounter = core.audioFrameCounter();\n\n    // Emit RomLoaded event\
    if (impl_->pluginManager.initialized()) {\n        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{\n            BMMQ::MachineEventType::RomLoaded,\
            BMMQ::PluginCategory::System,\
            0u,\
            0u,\
            0u,\
            nullptr,\
            \"ROM loaded\"\
        });\
    }\n}\n\nvoid GameBoyMachine::loadRomFromPath(const std::filesystem::path& path) {\n    std::ifstream input(path, std::ios::binary);\
    if (!input) {\n        throw std::runtime_error(\"Unable to open ROM: \" + path.string());\
    }\n    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),\
                                std::istreambuf_iterator<char>());\
    if (bytes.empty()) {\n        throw std::runtime_error(\"ROM is empty: \" + path.string());\
    }\n\n    setRomSourcePath(path);\
    loadRom(bytes);\
}\n\nvoid GameBoyMachine::loadExternalBootRom(const std::vector<uint8_t>& bytes) {\n    if (bytes.size() != 0x100u) {\n        throw std::invalid_argument(\"Game Boy boot ROM must be exactly 256 bytes\");\
    }\n    impl_->memoryMap.mapBootRom(bytes.data(), bytes.size());\
    impl_->memoryMap.setIoRegisterRaw(0xFF50u, 0x00u);\
    impl_->context->writeRegister16(GB::RegisterId::PC, 0x0000u);\
}\n\nvoid GameBoyMachine::setRomSourcePath(const std::optional<std::filesystem::path>& path) {\n    impl_->pendingRomSourcePath = path;\
}\n\nBMMQ::RuntimeContext& GameBoyMachine::runtimeContext() {\n    return *impl_->context;\n}\n\nconst BMMQ::RuntimeContext& GameBoyMachine::runtimeContext() const {\n    return *impl_->context;\
}\n\nBMMQ::PluginManager& GameBoyMachine::pluginManager() {\n    return impl_->pluginManager;\
}\n\nconst BMMQ::PluginManager& GameBoyMachine::pluginManager() const {\n    return impl_->pluginManager;\
}\n\nstd::span<const BMMQ::IoRegionDescriptor> GameBoyMachine::describeIoRegions() const {\n    return kIoRegions;\
}\n\nvoid GameBoyMachine::attachExecutorPolicy(BMMQ::Plugin::IExecutorPolicyPlugin& policy) {\n    BMMQ::Plugin::validateExecutorPolicyStartup(policy);\
    impl_->activePolicy = &policy;\
    impl_->context->refreshExecutionMode();\
}\n\nconst BMMQ::Plugin::IExecutorPolicyPlugin& GameBoyMachine::attachedExecutorPolicy() const {\n    return *impl_->activePolicy;\
}\n\nstd::vector<int16_t> GameBoyMachine::recentAudioSamples() const override {\n    return impl_->apu.copyRecentSamples();\
}\n\nuint32_t GameBoyMachine::audioSampleRate() const override {\n    return impl_->apu.sampleRate();\
}\n\nuint8_t GameBoyMachine::audioChannelCount() const override {\n    return 1u;\
}\n\nuint64_t GameBoyMachine::audioFrameCounter() const override {\n    return impl_->apu.frameCounter();\
}\n\nstd::string_view GameBoyMachine::visualTargetId() const noexcept override {\n    return \"gameboy\";\
}\n\nconst BMMQ::IVisualDebugAdapter* GameBoyMachine::visualDebugAdapter() const noexcept override {\n    return &gameBoyVisualDebugAdapter();\
}\n\nstd::optional<BMMQ::VideoDebugFrameModel> GameBoyMachine::videoDebugFrameModel(\
    const BMMQ::VideoDebugRenderRequest& request) const override {\n    return gameBoyVisualDebugAdapter().buildFrameModel(*this, request);\
}\n\nstd::optional<BMMQ::RealtimeVideoPacket> GameBoyMachine::realtimeVideoPacket(\
    const BMMQ::VideoDebugRenderRequest& request) const override {\n    return impl_->ppu.buildRealtimeFrame(request);\
}\n\nstd::optional<BMMQ::RealtimeAudioPacket> GameBoyMachine::realtimeAudioPacket() const {\n    BMMQ::RealtimeAudioPacket packet;\
    packet.sampleRate = impl_->apu.sampleRate();\
    packet.channelCount = 1u;\
    packet.frameCounter = impl_->apu.frameCounter();\
    packet.pcmSamples = impl_->apu.takePendingSamples();\
    return packet;\
}\n\nbool GameBoyMachine::flushCartridgeSave() {\n    if (!impl_->cartridge_.supportsBatterySave()) return false;\
    auto extracted = impl_->saveManager.extractDirtySaveSnapshot(impl_->cartridge_);\
    if (!extracted.has_value()) return false;\
    GB::CartridgeSaveManager::flushSnapshot(std::move(*extracted));\
    return true;\
}\n\nuint16_t GameBoyMachine::readRegisterPair(std::string_view id) const override {\n    return impl_->context->readRegister16(id);\
}\n\nstd::string GameBoyMachine::stopSummary() const override {\n    const auto pc = impl_->context->readRegister16(GB::RegisterId::PC);\
    const auto ly = impl_->context->read8(0xFF44);\
    const auto lcdc = impl_->context->read8(0xFF40);\
    const auto stat = impl_->context->read8(0xFF41);\
    const auto interruptFlags = impl_->context->read8(0xFF0F);\
    const auto interruptEnable = impl_->context->read8(0xFFFF);\n\n    std::ostringstream out;\
    out << \"PC=0x\" << std::hex << std::uppercase << pc << std::dec << '\\n'\
        << \"I/O state: LY=0x\" << std::hex << std::uppercase << static_cast<int>(ly)\
        << \" LCDC=0x\" << static_cast<int>(lcdc)\
        << \" STAT=0x\" << static_cast<int>(stat)\
        << \" IF=0x\" << static_cast<int>(interruptFlags)\
        << \" IE=0x\" << static_cast<int>(interruptEnable)\
        << std::dec;\
    return out.str();\
}\n\nvoid GameBoyMachine::setJoypadState(uint8_t value) {\n    impl_->runtime.cpu().setJoypadState(value);\
    impl_->input.setLogicalButtons(value);\
    impl_->lastDigitalInputMask = value;\
    impl_->memoryMap.write(0xFF00u, impl_->input.readRegister());\n\n    if (impl_->pluginManager.initialized()) {\n        impl_->pluginManager.emit(view(), BMMQ::MachineEvent{\
            BMMQ::MachineEventType::DigitalInputChanged,\
            BMMQ::PluginCategory::DigitalInput,\
            impl_->stepCounter,\
            0xFF00u,\
            impl_->input.readRegister(),\
            nullptr,\
            \"joypad state changed\"\
        });\
    }\n}\n\nGameBoyMachine::BlockCacheStats GameBoyMachine::blockCacheStats() const {\n    const auto stats = impl_->runtime.cpu().blockCacheStats();\
    return BlockCacheStats{\
        stats.hits.load(std::memory_order_relaxed),\
        stats.misses.load(std::memory_order_relaxed),\
        stats.invalidations.load(std::memory_order_relaxed)\
    };\n}\n\nbool GameBoyMachine::blockCacheEnabled() const {\n    return impl_->runtime.cpu().blockCacheEnabled();\
}\n\nvoid GameBoyMachine::setBlockCacheEnabled(bool enabled) {\n    impl_->runtime.cpu().setBlockCacheEnabled(enabled);\
}\n\n// Register access\
uint16_t GameBoyMachine::readRegisterPair(std::string_view id) const override {\n    return impl_->context->readRegister16(id);\
}\n\nstd::string GameBoyMachine::stopSummary() const override {\n    const auto pc = impl_->context->readRegister16(GB::RegisterId::PC);\
    const auto ly = impl_->context->read8(0xFF44);\
    const auto lcdc = impl_->context->read8(0xFF40);\
    const auto stat = impl_->context->read8(0xFF41);\
    const auto interruptFlags = impl_->context->read8(0xFF0F);\
    const auto interruptEnable = impl_->context->read8(0xFFFF);\n\n    std::ostringstream out;\
    out << \"PC=0x\" << std::hex << std::uppercase << pc << std::dec << '\\n'\
        << \"I/O state: LY=0x\" << std::hex << std::uppercase << static_cast<int>(ly)\
        << \" LCDC=0x\" << static_cast<int>(lcdc)\
        << \" STAT=0x\" << static_cast<int>(stat)\
        << \" IF=0x\" << static_cast<int>(interruptFlags)\
        << \" IE=0x\" << static_cast<int>(interruptEnable)\
        << std::dec;\
    return out.str();\
}\n\n} // namespace GB\n"
