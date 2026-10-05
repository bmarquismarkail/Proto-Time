#pragma once
#include "ExecutionContract.hpp"
#include "Project.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include <map>
namespace BMMQ::Space {
class ExecutionPaused : public std::runtime_error {public:using std::runtime_error::runtime_error;};
class Execution final : public ExecutionController, public RegisterExecutionView,
                        public IMemory<uint16_t,uint8_t,uint16_t> {
public:
    Execution(GB::GameBoyMachine&,Capture&,std::shared_ptr<StateBudget> budget={});
    ~Execution();
    void mode(const std::string&);
    bool enabled() const noexcept{return snapshot_;}
    void preflight() override;
    IMemory<uint16_t,uint8_t,uint16_t>* begin(std::span<const uint8_t>,uint64_t,
        MemoryPool<uint16_t,uint8_t,uint16_t>&,bool) override;
    void retired(Record&) override;
    void canonicalWrite(uint16_t,uint8_t) override;
    RegisterFile<uint16_t>& executionRegisters() override;
    void publishRegisters() override;
    void read(std::span<uint8_t>,uint16_t) override;
    void write(std::span<const uint8_t>,uint16_t) override;
    Json status() const;
    void evidenceLost(){if(snapshot_&&capture_.stopped())reason_="capture evidence lost or exhausted; explicit baseline required";}
    Json state() const;
    void restore(const Json&,bool newBranch=true,bool verifyCanonical=true);
    size_t chargedBytes() const noexcept {return charged_;}
    void adoptBudget(std::shared_ptr<StateBudget>);
private:
    struct Key {uint64_t location=0;std::array<uint8_t,3> bytes{};uint8_t length=0;auto operator<=>(const Key&)const=default;};
    struct Cell {uint64_t sequence=0,block=0;uint16_t value=0;bool known=false;uint64_t epoch=0;};
    struct Block {
        MemoryStorage<uint16_t,uint8_t> backing;
        MemorySnapshot<uint16_t,uint8_t,uint16_t> snapshot{backing};
        std::vector<Key> instructions;
        uint64_t id=0,visit=0;
        Block();
    };
    static constexpr size_t blockCharge=512u*1024u,baseCharge=3u*1024u*1024u;
    static uint16_t normalize(uint16_t);
    static bool ram(uint16_t);
    static Json keyJson(const Key&);
    static Key parseKey(const Json&);
    [[noreturn]] void pause(const char*);
    void clear();
    void traceRead(uint16_t,uint8_t,uint8_t,const Cell&);
    uint16_t lane(size_t,const RegisterFile<uint16_t>&) const;
    GB::GameBoyMachine& machine_;
    Capture& capture_;
    std::shared_ptr<StateBudget> budget_;
    MemoryPool<uint16_t,uint8_t,uint16_t>& canonical_;
    GB::GameBoyMemoryMap& bus_;
    std::vector<Cell> values_;
    std::array<Cell,10> registers_{};
    std::vector<std::unique_ptr<Block>> blocks_;
    std::map<Key,std::pair<uint64_t,size_t>> owners_;
    std::unique_ptr<Block> pending_;
    Block* active_=nullptr;
    OperandInfo operands_;
    bool snapshot_=false,ramBlocked_=false,executing_=false;
    std::string reason_,epoch_,requestedMode_="baseline";
    uint64_t epochId_=0,visits_=0,sequence_=0,snapshotInstructions_=0,baselineInstructions_=0,boundaries_=0,snapshotReads_=0,busReads_=0;
    size_t charged_=0;
};
}
