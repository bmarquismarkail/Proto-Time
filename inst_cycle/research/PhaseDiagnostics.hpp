#pragma once
// Compile-time diagnostic lane. Throughput builds contain no clocks/counters.
#ifdef TIME_RESEARCH_PHASES
#include <array>
#include <chrono>
#include <cstdint>
namespace BMMQ::IR::Research {
enum class Phase : unsigned { Bind, Remap, EntryGuard, ContinuationGuard, Operations, Helper, Helper0, CpuRetirement, MachineRetirement, Count };
struct PhaseData {std::uint64_t calls=0,samples=0,nanoseconds=0;std::uint32_t rng=0xabcdef01u;};
struct PhaseLedger {
  std::array<PhaseData,static_cast<unsigned>(Phase::Count)> phases{};
  std::uint64_t metadataCopies=0,estimatedMetadataBytes=0;
  PhaseLedger(){for(unsigned i=0;i<phases.size();++i)phases[i].rng^=(i+1)*0x9e3779b9u;}
};
inline thread_local PhaseLedger* currentLedger=nullptr;
struct PhaseSession {PhaseLedger* previous;explicit PhaseSession(PhaseLedger& l):previous(currentLedger){currentLedger=&l;}~PhaseSession(){currentLedger=previous;}};
inline PhaseData* samplePhase(Phase p) {
  if(!currentLedger)return nullptr;
  auto& d=currentLedger->phases[static_cast<unsigned>(p)];++d.calls;
  d.rng^=d.rng<<13;d.rng^=d.rng>>17;d.rng^=d.rng<<5;
  if(d.rng&63u)return nullptr;
  ++d.samples;return &d;
}
struct ScopedPhase {
  PhaseData* data;std::chrono::steady_clock::time_point start{};
  explicit ScopedPhase(Phase p):data(samplePhase(p)){if(data)start=std::chrono::steady_clock::now();}
  ~ScopedPhase(){if(data)data->nanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
};
inline thread_local PhaseData* operationData=nullptr;
inline thread_local std::chrono::steady_clock::time_point operationStart{};
inline void beginOperations(){operationData=samplePhase(Phase::Operations);if(operationData)operationStart=std::chrono::steady_clock::now();}
inline void endOperations(){if(operationData)operationData->nanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-operationStart).count();operationData=nullptr;}
inline void recordMetadataCopy(const Block& block) {
  if(!currentLedger)return;
  ++currentLedger->metadataCopies;auto& n=currentLedger->estimatedMetadataBytes;
  n+=sizeof(Block)+block.instructions.size()*sizeof(GuestInstruction)+block.guards.size()*sizeof(Guard);
  for(const auto& i:block.instructions){n+=i.operations.size()*sizeof(Operation);for(const auto& o:i.operations)n+=o.operands.size()*sizeof(Operand);}
  for(const auto& g:block.guards)n+=g.bytes.size();
}
}
#define TIME_RESEARCH_SCOPE(name, phase) BMMQ::IR::Research::ScopedPhase name(phase)
#define TIME_RESEARCH_COPY(block) BMMQ::IR::Research::recordMetadataCopy(block)
#define TIME_RESEARCH_OPS_BEGIN() BMMQ::IR::Research::beginOperations()
#define TIME_RESEARCH_OPS_END() BMMQ::IR::Research::endOperations()
#else
#define TIME_RESEARCH_SCOPE(name, phase)
#define TIME_RESEARCH_COPY(block)
#define TIME_RESEARCH_OPS_BEGIN()
#define TIME_RESEARCH_OPS_END()
#endif
