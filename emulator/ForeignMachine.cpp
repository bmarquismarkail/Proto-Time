#include "ForeignMachine.hpp"
#include "machine/SaveState.hpp"
#include "machine/modding/NativeMod.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include <openssl/sha.h>
#include <array>
#include <cstring>
#include <limits>

namespace BMMQ {
namespace {
constexpr std::uint32_t StateBudget = 32u * 1024u * 1024u;
std::string romDigest(std::span<const std::uint8_t> bytes) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
    SHA256(bytes.data(), bytes.size(), hash.data());
    std::string value;
    for (auto b : hash) { value += "0123456789abcdef"[b >> 4]; value += "0123456789abcdef"[b & 15]; }
    return value;
}
struct ApiOwner {
    std::shared_ptr<const void> module;
    TimeMachineRuntimeV2 api{};
    ~ApiOwner() { if (api.destroy) { try { api.destroy(api.context); } catch (...) {} } }
};
class ForeignMachine final : public Machine, public IExternalBootRomMachine, public IForeignMachineRuntimeV2 {
    struct Runtime final : RuntimeContext {
        ForeignMachine& owner;
        explicit Runtime(ForeignMachine& m) : owner(m) {}
        FetchBlock fetch() override { throw std::invalid_argument("foreign runtime uses module-owned instruction retirement"); }
        ExecutionBlock decode(FetchBlock&) override { throw std::invalid_argument("foreign decode is module-owned"); }
        void execute(const ExecutionBlock&, FetchBlock&) override { throw std::invalid_argument("foreign execute is module-owned"); }
        CpuFeedback step() override { return owner.retire(); }
        DataType read8(AddressType a) const override { return owner.read(a, false); }
        DataType peek8(AddressType a) const override { return owner.read(a, true); }
        void write8(AddressType a, DataType v) override { owner.call([&] { return owner.api().write8(owner.api().context,a,v); }); }
        std::uint8_t readRegister8(std::string_view n) const override { return static_cast<std::uint8_t>(owner.reg(n,8)); }
        std::uint16_t readRegister16(std::string_view n) const override { return owner.reg(n,16); }
        void writeRegister8(std::string_view n,std::uint8_t v) override { owner.setReg(n,8,v); }
        void writeRegister16(std::string_view n,std::uint16_t v) override { owner.setReg(n,16,v); }
        const CpuFeedback& getLastFeedback() const override { return owner.feedback_; }
        std::uint32_t clockHz() const override { return owner.clock_; }
        ExecutionGuarantee guarantee() const override { return ExecutionGuarantee::BaselineFaithful; }
        const Plugin::PluginMetadata* attachedPolicyMetadata() const override { return &owner.policy_->metadata(); }
        const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return *owner.policy_; }
    } runtime_{*this};
    struct Prepared {
        ForeignMachine& owner;
        void* token = nullptr;
        Prepared(ForeignMachine& m,std::uint32_t op,std::span<const std::uint8_t> bytes) : owner(m) {
            if (bytes.empty() || bytes.size() > StateBudget) throw std::invalid_argument("foreign transition byte budget");
            try { owner.call([&] { return owner.api().prepare(owner.api().context,op,bytes.data(),bytes.size(),&token); }); }
            catch (...) { cleanup(); throw; }
            if (!token) throw std::runtime_error("foreign transition returned no transaction");
        }
        void cleanup() noexcept { if(token) { try { owner.api().discard(owner.api().context,token); } catch (...) { owner.fault_ = true; } token=nullptr; } }
        ~Prepared() { cleanup(); }
        Prepared(const Prepared&) = delete;
        Prepared& operator=(const Prepared&) = delete;
        void commit() {
            // The ABI makes this the non-failing publication boundary.
            auto* consuming = std::exchange(token,nullptr);
            try { owner.api().commit(owner.api().context,consuming); }
            catch (...) { owner.fault_ = true; throw std::runtime_error("foreign commit violated nonthrowing contract"); }
        }
    };
    ApiOwner apiOwner_;
    MachineDescriptor descriptor_;
    std::uint32_t clock_;
    std::vector<std::string> labels_;
    std::vector<IoRegionDescriptor> regions_;
    PluginManager plugins_;
    std::unique_ptr<Plugin::IExecutorPolicyPlugin> policy_ = std::make_unique<Plugin::DefaultStepPolicy>();
    std::vector<std::uint8_t> rom_;
    std::string binding_;
    CpuFeedback feedback_{};
    bool fault_ = false;
    std::array<TimeMachineEventV2,256> events_{};
    std::size_t eventCount_ = 0;
    bool eventFault_ = false;
    bool machineBoundary_ = false;
    std::uint32_t inputMask_ = 0;
    const TimeMachineRuntimeV2& api() const { return apiOwner_.api; }
    template<class F> void call(F&& f) const {
        if (fault_) throw std::runtime_error("foreign machine is faulted");
        int accepted;
        try { accepted = f(); } catch (...) { throw std::runtime_error("foreign callback threw across C ABI"); }
        if (accepted != 1) throw std::runtime_error("foreign callback rejected operation");
    }
    static int32_t recordEvent(void* opaque,const TimeMachineEventV2* event) noexcept {
        auto& m=*static_cast<ForeignMachine*>(opaque);
        if (!event || event->struct_size != sizeof(*event) || event->type > 19u || event->category > 7u || event->has_feedback > 1 ||
            !std::memchr(event->message,0,sizeof(event->message)) || m.eventCount_ == m.events_.size()) {
            m.eventFault_=true; return 0;
        }
        if(event->has_feedback && (event->feedback.struct_size!=sizeof(event->feedback) || event->feedback.pc_before>65535 ||
            event->feedback.pc_after>65535 || event->feedback.segment_boundary>1 || event->feedback.control_flow>1 || event->feedback.execution_path>5)) {
            m.eventFault_=true;return 0;
        }
        m.events_[m.eventCount_++]=*event; return 1;
    }
    CpuFeedback retire() {
        eventCount_=0; eventFault_=false;
        TimeMachineRetirementV2 retired{sizeof(retired),0,0,0,0,0,0,0};
        const TimeMachineEventSinkV2 sink{sizeof(sink),this,recordEvent};
        try {
            call([&] { return api().step(api().context,&retired,&sink); });
            if (eventFault_ || retired.struct_size != sizeof(retired) || !retired.cycles ||
                retired.pc_before > 65535 || retired.pc_after > 65535 || retired.segment_boundary > 1 || retired.control_flow > 1 || retired.execution_path > 5 || retired.machine_boundary > 1)
                throw std::runtime_error("invalid foreign retirement or exhausted event handoff");
        } catch (...) { fault_=true; throw; }
        feedback_={retired.segment_boundary != 0,retired.control_flow != 0,retired.pc_before,retired.pc_after,
            retired.cycles,static_cast<ExecutionPathHint>(retired.execution_path)};
        machineBoundary_=retired.machine_boundary!=0;
        return feedback_;
    }
    std::uint8_t read(std::uint16_t a,bool inspection) const {
        std::uint8_t value=0; call([&] { return api().read8(api().context,a,inspection,&value); }); return value;
    }
    std::uint16_t reg(std::string_view name,std::uint32_t width) const {
        if (name.empty() || name.size()>32 || name.find('\0') != name.npos) throw std::invalid_argument("foreign register name");
        const std::string owned(name); std::uint16_t value=0;
        call([&] { return api().read_register(api().context,owned.c_str(),width,&value); });
        if (width==8 && value>255) throw std::runtime_error("foreign 8-bit register overflow");
        return value;
    }
    void setReg(std::string_view name,std::uint32_t width,std::uint16_t value) {
        (void)reg(name,width); const std::string owned(name);
        call([&] { return api().write_register(api().context,owned.c_str(),width,value); });
    }
    RealtimeAudioPacket audioCopy() const {
        TimeMachineAudioV2 meta{sizeof(meta),0,0,0,0};
        call([&] { return api().audio(api().context,&meta,nullptr,0); });
        if (meta.struct_size!=sizeof(meta) || meta.sample_rate<8000 || meta.sample_rate>192000 ||
            !meta.channels || meta.channels>2 || meta.sample_count>65536 || meta.sample_count%meta.channels)
            throw std::runtime_error("foreign audio bounds rejected");
        const auto first=meta;
        RealtimeAudioPacket packet; packet.pcmSamples.resize(meta.sample_count);
        call([&] { return api().audio(api().context,&meta,packet.pcmSamples.data(),packet.pcmSamples.size()); });
        if(meta.struct_size!=first.struct_size || meta.sample_rate!=first.sample_rate || meta.channels!=first.channels ||
            meta.sample_count!=first.sample_count || meta.frame!=first.frame) throw std::runtime_error("unstable foreign audio copy");
        packet.sampleRate=meta.sample_rate;packet.channelCount=meta.channels;packet.frameCounter=meta.frame;
        return packet;
    }
protected:
    InstructionRetirementDecision onInstructionRetired(const CpuFeedback&,const ExecutionSliceProgress&) override {
        for (std::size_t i=0;i<eventCount_;++i) {
            const auto& e=events_[i];
            const auto& f=e.feedback;
            const CpuFeedback eventFeedback{f.segment_boundary!=0,f.control_flow!=0,f.pc_before,f.pc_after,f.cycles,static_cast<ExecutionPathHint>(f.execution_path)};
            plugins_.emit(view(),MachineEvent{static_cast<MachineEventType>(e.type),static_cast<PluginCategory>(e.category),
                e.step,e.address,e.value,e.has_feedback ? &eventFeedback : nullptr,e.message});
        }
        return machineBoundary_ ? InstructionRetirementDecision::exitSlice(ExecutionSliceExitReason::MachineBoundary) : InstructionRetirementDecision::continueSlice();
    }
public:
    ForeignMachine(std::shared_ptr<const void> module,TimeMachineRuntimeProviderV2 p,MachineDescriptor descriptor,
                   std::vector<std::string> labels,std::vector<IoRegionDescriptor> regions)
        : apiOwner_{module,{}},descriptor_(std::move(descriptor)),clock_(p.clock_hz),labels_(std::move(labels)),regions_(std::move(regions)) {
        auto& a=apiOwner_.api; a.struct_size=sizeof(a);a.abi_version=TIME_MACHINE_RUNTIME_ABI_V2;
        int accepted;
        try { accepted=p.create(&a); } catch (...) { throw std::runtime_error("foreign factory threw across C ABI"); }
        if (accepted!=1 || a.struct_size!=sizeof(a) || a.abi_version!=TIME_MACHINE_RUNTIME_ABI_V2 || !a.context ||
            !a.destroy || !a.prepare || !a.commit || !a.discard || !a.step || !a.read8 || !a.write8 ||
            !a.read_register || !a.write_register || !a.input || !a.checkpoint || !a.fingerprint || !a.video || !a.audio)
            throw std::invalid_argument("foreign machine runtime table rejected");
        for(std::size_t i=0;i<regions_.size();++i) regions_[i].label=labels_[i];
        retainProviderLifetime(std::move(module));
    }
    ~ForeignMachine() override { try { plugins_.shutdown(mutableView()); } catch (...) {} }
    RuntimeContext& runtimeContext() override { return runtime_; }
    const RuntimeContext& runtimeContext() const override { return runtime_; }
    ExecutionSliceResult runSlice(const ExecutionBudget& budget,InstructionRetirementSink* observer=nullptr) override {
        if(budget.maxInstructions && budget.maxCycles) {
            const auto mask=inputService().committedDigitalMask().value_or(inputMask_);
            call([&] { return api().input(api().context,mask); });inputMask_=mask;
        }
        return Machine::runSlice(budget,observer);
    }
    PluginManager& pluginManager() override { return plugins_; }
    const PluginManager& pluginManager() const override { return plugins_; }
    std::span<const IoRegionDescriptor> describeIoRegions() const override { return regions_; }
    const Plugin::IExecutorPolicyPlugin& attachedExecutorPolicy() const override { return *policy_; }
    void attachExecutorPolicy(const Plugin::IExecutorPolicyPlugin& p) override {
        Plugin::validateExecutorPolicyForRuntime(p,runtime_);
        if (p.metadata().id != policy_->metadata().id || p.backend()!=ExecutionBackend::Baseline)
            throw std::invalid_argument("foreign runtime supports the default baseline policy only");
        auto next=p.clone();
        if(!next) throw std::invalid_argument("foreign executor policy clone is empty");
        Plugin::validateExecutorPolicyForRuntime(*next,runtime_);
        if(next->metadata().id!=policy_->metadata().id || next->backend()!=ExecutionBackend::Baseline)
            throw std::invalid_argument("foreign executor policy clone changed capabilities");
        policy_=std::move(next);
    }
    std::uint16_t readRegisterPair(std::string_view name) const override { return reg(name,16); }
    std::string_view visualTargetId() const noexcept override { return descriptor_.familyId; }
    std::string stopSummary() const override { return fault_ ? "foreign runtime fault; no fallback" : ""; }
    void loadExternalBootRom(const std::vector<std::uint8_t>& bytes) override {
        if(!rom_.empty() || bytes.size()>32768) throw std::invalid_argument("foreign BIOS is construction-only");
        Prepared pending(*this,TIME_MACHINE_PREPARE_BIOS,bytes);pending.commit();
    }
    void loadRom(const std::vector<std::uint8_t>& bytes) override {
        if (bytes.empty() || bytes.size()>StateBudget) throw std::invalid_argument("foreign ROM budget");
        auto owned=bytes; auto binding=descriptor_.id+"\n"+descriptor_.familyId+"\n2\n"+romDigest(bytes);
        auto native=Modding::NativeMod::prepareReset(modHost());
        Prepared pending(*this,TIME_MACHINE_PREPARE_ROM,bytes);
        if (!lifecycleCoordinator().runTransition(MachineTransitionReason::RomLoad,[&] {
            native.commit();pending.commit();modHost().reset();rom_.swap(owned);binding_.swap(binding);
            advanceObservationGeneration();inputService().advanceGeneration(observationGeneration());inputMask_=0;
            inputService().publishDigitalSnapshot(0,observationGeneration());
            feedback_={};eventCount_=0;
            return MachineTransitionMutationResult{};
        })) throw std::runtime_error("foreign ROM lifecycle transition failed");
        plugins_.emit(view(),MachineEvent{MachineEventType::RomLoaded,PluginCategory::System,0,0,0,nullptr,"foreign ROM loaded"});
    }
    void serviceInput() override {
        if (inputService().state()==InputLifecycleState::Active) (void)inputService().pollActiveAdapter(observationGeneration());
        const auto mask=inputService().committedDigitalMask().value_or(inputMask_);
        call([&] { return api().input(api().context,mask); });inputMask_=mask;
    }
    std::optional<std::uint32_t> currentDigitalInputMask() const override { return inputMask_; }
    std::optional<RealtimeVideoSubmission> realtimeVideoPacket(const VideoDebugRenderRequest& request) const override {
        if(request.frameWidth!=descriptor_.defaultFrameWidth || request.frameHeight!=descriptor_.defaultFrameHeight)
            throw std::invalid_argument("foreign frame extent unsupported");
        TimeMachineVideoV2 meta{sizeof(meta),0,0,0,0,UINT32_MAX};
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(descriptor_.defaultFrameWidth)*descriptor_.defaultFrameHeight);
        call([&] { return api().video(api().context,&meta,pixels.data(),pixels.size()); });
        if(meta.struct_size!=sizeof(meta) || meta.width!=static_cast<std::uint32_t>(request.frameWidth) ||
            meta.height!=static_cast<std::uint32_t>(request.frameHeight) || meta.display_enabled>1 || meta.in_vblank>1 ||
            (meta.scanline!=UINT32_MAX && meta.scanline>65535)) throw std::runtime_error("foreign video bounds rejected");
        RealtimeVideoPacket packet;packet.width=meta.width;packet.height=meta.height;
        packet.displayEnabled=meta.display_enabled;packet.inVBlank=meta.in_vblank;
        if(meta.scanline!=UINT32_MAX) packet.scanlineIndex=meta.scanline;
        packet.surface=makeArgbVideoSurface(std::move(pixels),packet.width,packet.height);
        return RealtimeVideoSubmission{std::move(packet),{}};
    }
    std::optional<RealtimeAudioPacket> realtimeAudioPacket() const override { return audioCopy(); }
    std::vector<std::int16_t> recentAudioSamples() const override { return audioCopy().pcmSamples; }
    std::uint32_t audioSampleRate() const override { return audioCopy().sampleRate; }
    std::uint8_t audioChannelCount() const override { return audioCopy().channelCount; }
    std::uint64_t audioFrameCounter() const override { return audioCopy().frameCounter; }
    std::string deterministicStateFingerprint() const override {
        std::array<char,129> text{};call([&] { return api().fingerprint(api().context,text.data(),text.size()); });
        const auto n=strnlen(text.data(),text.size());
        if(!n || n==text.size()) throw std::runtime_error("foreign fingerprint bounds rejected");
        const std::string moduleFingerprint(text.data(),n);
        const auto native=Modding::NativeMod::saveCohort(modHost());
        if(!modHost().hasRegions() && native.size()<=8) return moduleFingerprint;
        std::vector<std::uint8_t> combined{'T','I','M','E','-','F','O','R','E','I','G','N','-','2'};
        auto append=[&](std::span<const std::uint8_t> bytes) {
            const auto size=static_cast<std::uint32_t>(bytes.size());
            for(unsigned shift=0;shift<32;shift+=8) combined.push_back(static_cast<std::uint8_t>(size>>shift));
            combined.insert(combined.end(),bytes.begin(),bytes.end());
        };
        append({reinterpret_cast<const std::uint8_t*>(moduleFingerprint.data()),moduleFingerprint.size()});
        append(modHost().exportState());append(native);
        return romDigest(combined);
    }
    void save_state(const std::filesystem::path& path) override {
        if(rom_.empty()) throw std::invalid_argument("foreign save before ROM");
        std::uint32_t size=0;call([&] { return api().checkpoint(api().context,nullptr,0,&size); });
        if(!size || size>StateBudget) throw std::runtime_error("foreign checkpoint budget");
        std::vector<std::uint8_t> bytes(size);const auto expected=size;
        call([&] { return api().checkpoint(api().context,bytes.data(),bytes.size(),&size); });
        if(size!=expected) throw std::runtime_error("unstable foreign checkpoint copy");
        SaveStateFile file;std::memcpy(file.header.magic,kSaveStateMagic,5);file.header.version=kSaveStateVersion;
        file.header.core_id=descriptor_.familyId=="gameboy"?kCoreId_GameBoy:kCoreId_GameGear;
        file.header.rom_hash=crc32(rom_.data(),rom_.size());file.header.checksum=SaveStateChecksum::Crc32;
        auto add=[&](std::string name,std::vector<std::uint8_t> data) { file.chunks.push_back({std::move(name),static_cast<std::uint32_t>(data.size()),std::move(data)}); };
        add("foreign.binding",{binding_.begin(),binding_.end()});add("foreign.state",std::move(bytes));
        add("foreign.input",{static_cast<std::uint8_t>(inputMask_)});
        add("time.host-regions",modHost().exportState());add("time.native-mods",Modding::NativeMod::saveCohort(modHost()));
        file.header.chunk_count=file.chunks.size();SaveStateReader::write(file,path);
    }
    void load_state(const std::filesystem::path& path) override {
        if(rom_.empty() || std::filesystem::file_size(path)>2*StateBudget) throw std::invalid_argument("foreign restore budget or unloaded ROM");
        const auto file=SaveStateReader::readForCore(path,descriptor_.familyId=="gameboy"?kCoreId_GameBoy:kCoreId_GameGear,rom_);
        if(file.chunks.size()!=5) throw std::invalid_argument("foreign checkpoint chunk count");
        auto chunk=[&](std::string_view name)->std::span<const std::uint8_t> {
            const SaveStateChunk* found=nullptr;for(const auto& c:file.chunks) if(c.name==name) { if(found) throw std::invalid_argument("duplicate foreign chunk");found=&c; }
            if(!found) throw std::invalid_argument("missing foreign chunk");
            return found->data;
        };
        const auto binding=chunk("foreign.binding");
        if(std::string_view(reinterpret_cast<const char*>(binding.data()),binding.size())!=binding_) throw std::invalid_argument("foreign checkpoint provider/ROM mismatch");
        const auto input=chunk("foreign.input");
        if(input.size()!=1) throw std::invalid_argument("foreign checkpoint input state");
        const auto nextInput=input.front();
        auto native=Modding::NativeMod::prepareCohort(modHost(),chunk("time.native-mods"),chunk("time.host-regions"));
        Prepared pending(*this,TIME_MACHINE_PREPARE_STATE,chunk("foreign.state"));
        // Service queues stop before either complete state becomes visible.
        if(!lifecycleCoordinator().runTransition(MachineTransitionReason::ConfigReconfigure,[&] {
            native.commit();pending.commit();advanceObservationGeneration();inputService().advanceGeneration(observationGeneration());
            inputMask_=nextInput;inputService().publishDigitalSnapshot(nextInput,observationGeneration());
            eventCount_=0;feedback_={};return MachineTransitionMutationResult{};
        })) throw std::runtime_error("foreign restore lifecycle failed");
    }
};
}
std::unique_ptr<Machine> createForeignMachine(std::shared_ptr<const void> module,TimeMachineRuntimeProviderV2 provider,
    MachineDescriptor descriptor,std::vector<std::string> labels,std::vector<IoRegionDescriptor> regions) {
    return std::make_unique<ForeignMachine>(std::move(module),provider,std::move(descriptor),std::move(labels),std::move(regions));
}
}
