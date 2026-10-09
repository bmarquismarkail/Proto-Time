#include "CoreAdapter.hpp"
#include "Project.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
namespace BMMQ::Space {
namespace {
class GameBoyAdapter final:public CoreAdapter {
    GB::GameBoyMachine& m_;
    GB::GameBoyMemoryMap& bus_;
public:
    explicit GameBoyAdapter(GB::GameBoyMachine& m):m_(m),bus_(dynamic_cast<GB::GameBoyMemoryMap&>(m.executionMemory().backingStore())){}
    Machine& machine()override{return m_;}
    const CoreModel& model()const override{return coreModel("gameboy");}
    MemoryPool<uint16_t,uint8_t,uint16_t>& memory()override{return m_.executionMemory();}
    uint64_t location(uint16_t a,bool=false)const override{return bus_.analysisLocation(a);}
    uint8_t rawRead(uint16_t a)const override{return bus_.read(a);}
    uint16_t normalize(uint16_t a)const override{
        if(a>=0xe000&&a<=0xfdff)return uint16_t(a-0x2000);
        size_t offset=0;if(m_.cartridge().analysisRamOffset(a,offset)){
            const auto size=m_.cartridge().metadata().mapper==GB::CartridgeMapper::MBC2?0x200u:0x2000u;
            return uint16_t(0xa000+offset%size);
        }
        return a;
    }
    bool ram(uint16_t a)const override{
        size_t offset=0;if(m_.cartridge().analysisRamOffset(a,offset))return true;
        a=normalize(a);return (a>=0xc000&&a<0xe000)||(a>=0xff80&&a<0xffff);
    }
    size_t ramCapacity()const noexcept override{return 65536+m_.cartridge().analysisRamCapacity();}
    size_t ramIndex(uint16_t a)const override{
        size_t offset=0;if(m_.cartridge().analysisRamOffset(a,offset))return 65536+offset;
        return ram(a)?normalize(a):size_t(-1);
    }
    bool physicalRamRead(size_t index,uint8_t& value)const noexcept override{
        if(index>=65536)return m_.cartridge().analysisRamByte(index-65536,value);
        if(!((index>=0xc000&&index<0xe000)||(index>=0xff80&&index<0xffff)))return false;
        value=bus_.peek(uint16_t(index));return true;
    }
    uint64_t physicalRamLocation(size_t index)const override{
        if(index<65536)return Space::location(3,0,uint16_t(index));
        index-=65536;const auto size=m_.cartridge().metadata().mapper==GB::CartridgeMapper::MBC2?0x200u:0x2000u;
        return Space::location(3,uint16_t(index/size),uint16_t(index%size));
    }
    uint64_t fetchLocation(uint16_t pc,size_t offset)const override{return location(m_.analysisFetchAddress(pc,offset));}
    uint16_t nextFetchAddress(uint16_t pc,size_t offset)const override{return m_.analysisNextFetchAddress(pc,offset);}
    size_t physicalRamIndex(uint64_t key)const override{
        if((key>>32)!=3)return size_t(-1);
        const auto bank=uint16_t(key>>16),offset=uint16_t(key);
        if(bank==0&&((offset>=0xc000&&offset<0xe000)||(offset>=0xff80&&offset<0xffff)))return offset;
        const auto size=m_.cartridge().metadata().mapper==GB::CartridgeMapper::MBC2?0x200u:0x2000u;
        const auto index=65536+size_t(bank)*size+offset;
        return offset<size&&index<ramCapacity()?index:size_t(-1);
    }
    bool boundaryOnly()const override{return m_.snapshotBoundaryOnly();}
    bool opcodeSupported(uint8_t op)const override{return m_.snapshotOpcodeSupported(op);}
    std::string fingerprint()const override{return m_.deterministicStateFingerprint();}
    std::string romDigest()const override{return digest(m_.cartridge().romBytes());}
    void capture(Capture* c)override{m_.setAnalysisCapture(c);}
    void execution(ExecutionController* e)override{m_.setSnapshotExecution(e);}
    void input(uint8_t mask)override{m_.setJoypadState(mask);}
    std::unique_ptr<Machine> candidate(std::span<const uint8_t> rom)const override{auto m=std::make_unique<GB::GameBoyMachine>();m->loadRom({rom.begin(),rom.end()});m->modHost()=m_.modHost();return m;}
};
class GameGearAdapter final:public CoreAdapter {
    GameGearMachine& m_;
public:
    explicit GameGearAdapter(GameGearMachine& m):m_(m){}
    Machine& machine()override{return m_;}
    const CoreModel& model()const override{return coreModel("gamegear");}
    MemoryPool<uint16_t,uint8_t,uint16_t>& memory()override{return m_.executionMemory();}
    uint64_t location(uint16_t a,bool w=false)const override{return m_.analysisLocation(a,w);}
    uint8_t rawRead(uint16_t a)const override{return m_.analysisRead(a);}
    uint16_t normalize(uint16_t a)const override{return a>=0xe000?uint16_t(a-0x2000):a;}
    bool ram(uint16_t a)const override{
        const auto key=location(a);if((key>>32)!=3)return false;
        uint8_t ignored=0;return physicalRamRead(physicalRamIndex(key),ignored);
    }
    size_t ramCapacity()const noexcept override{return m_.analysisRamCapacity();}
    size_t ramIndex(uint16_t a)const override{
        if(!ram(a))return size_t(-1);
        const auto key=location(a);const auto bank=uint16_t(key>>16),offset=uint16_t(key);
        return bank?65536+size_t(bank-1)*0x4000+offset:offset;
    }
    bool physicalRamRead(size_t index,uint8_t& value)const noexcept override{return m_.analysisPhysicalRamByte(index,value);}
    uint64_t physicalRamLocation(size_t index)const override{
        if(index<65536)return Space::location(3,0,uint16_t(index));
        index-=65536;return Space::location(3,uint16_t(1+index/0x4000),uint16_t(index%0x4000));
    }
    size_t physicalRamIndex(uint64_t key)const override{
        if((key>>32)!=3)return size_t(-1);
        const auto bank=uint16_t(key>>16),offset=uint16_t(key);
        if(bank==0&&offset>=0xc000&&offset<0xe000)return offset;
        const auto index=65536+size_t(bank?bank-1:0)*0x4000+offset;
        return bank&&offset<0x4000&&index<ramCapacity()?index:size_t(-1);
    }
    bool boundaryOnly()const override{return m_.snapshotBoundaryOnly();}
    bool opcodeSupported(uint8_t)const override{return true;}
    std::string fingerprint()const override{return m_.deterministicStateFingerprint();}
    std::string romDigest()const override{return digest(m_.romData());}
    void prepareInstruction()override{(void)m_.snapshotInstruction();}
    void capture(Capture* c)override{m_.setAnalysisCapture(c);}
    void execution(ExecutionController* e)override{m_.setSnapshotExecution(e);}
    void input(uint8_t mask)override{m_.setAnalysisInput(mask);}
    std::unique_ptr<Machine> candidate(std::span<const uint8_t> rom)const override{auto m=std::make_unique<GameGearMachine>();m->loadRom({rom.begin(),rom.end()});m->modHost()=m_.modHost();return m;}
};
}
std::unique_ptr<CoreAdapter> makeCoreAdapter(Machine& m){
    if(auto* gb=dynamic_cast<GB::GameBoyMachine*>(&m))return std::make_unique<GameBoyAdapter>(*gb);
    if(auto* gg=dynamic_cast<GameGearMachine*>(&m))return std::make_unique<GameGearAdapter>(*gg);
    throw std::invalid_argument("machine has no S.P.A.C.E. core adapter");
}
}
