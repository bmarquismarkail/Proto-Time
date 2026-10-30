#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/research/HaltedAdvance.hpp"
#include "machine/AudioService.hpp"
#include "PerfTimingSupport.hpp"
#include <iostream>
#include <type_traits>
using namespace BMMQ;
using namespace BMMQ::IR::Research;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class Core> void prepare(Core& m){
 haltedAdvanceEnabled=false;std::vector<uint8_t> bytes(32768);bytes[0x100]=0x76;
 if constexpr(std::is_same_v<Core,GameGearMachine>){
   unsigned pc=0x100;
   for(uint8_t value:{uint8_t(0x80),uint8_t(0x10),uint8_t(0x90)}){
     bytes[pc++]=0x3e;bytes[pc++]=value;bytes[pc++]=0xd3;bytes[pc++]=0x7f;
   }
   bytes[pc]=0x76;
 }
 m.loadRom(bytes);
 if constexpr(std::is_same_v<Core,GB::GameBoyMachine>){
   m.runtimeContext().write8(0xffff,0);m.runtimeContext().write8(0xff0f,0);
   m.runtimeContext().writeRegister16("PC",0x100);m.step();
   for(const auto& [address,value]:std::array<std::pair<uint16_t,uint8_t>,7>{{{0xff26,0x80},{0xff24,0x77},{0xff25,0x11},{0xff11,0x80},{0xff12,0xf3},{0xff13,0},{0xff14,0x87}}})m.runtimeContext().write8(address,value);
 }else{
   auto r=m.debugRegisters();r[5]=0x100;r[4]=0xcf00;r[12]=0x20;r[17]=1;m.debugCommitRegisters(r);
   for(unsigned n=0;!m.debugRegisters()[18] && n<16;++n)m.step();
   require(m.debugRegisters()[18],"FIFO tone ROM did not halt");
 }
}
template<class Core> void check(const char* core){
 Core a,b;prepare(a);prepare(b);haltedAdvanceStats={};
 const auto channels=a.audioChannelCount();const size_t chunk=64u*channels;
 AudioService aa({.sourceSampleRate=int(a.audioSampleRate()),.deviceSampleRate=48000,.channelCount=channels});
 AudioService ab({.sourceSampleRate=int(b.audioSampleRate()),.deviceSampleRate=48000,.channelCount=channels});
 for(auto* audio:{&aa,&ab})require(audio->configureOutputTransport({.deviceSampleRate=48000,.channelCount=channels,.callbackChunkSamples=chunk,.readyQueueChunks=1}),"FIFO configure failed");
 std::vector<int16_t> x(chunk),y(chunk);std::vector<int64_t> durations;uint64_t last=0;bool audibleSamples=false;size_t occupancyLow=SIZE_MAX,occupancyHigh=0,occupancySamples=0;
 for(unsigned n=0;n<300;++n){
   haltedAdvanceEnabled=false;const auto ra=a.runSlice({1000,4000,false});
   haltedAdvanceEnabled=true;const auto rb=b.runSlice({1000,4000,false});
   require(ra.progress.retiredInstructions==rb.progress.retiredInstructions && ra.progress.retiredCycles==rb.progress.retiredCycles && ra.exitReason==rb.exitReason,"FIFO producer slice mismatch");
   require(a.audioFrameCounter()==b.audioFrameCounter(),"FIFO chunk clock mismatch");
   if(a.audioFrameCounter()!=last){
     last=a.audioFrameCounter();const auto pa=a.realtimeAudioPacket(),pb=b.realtimeAudioPacket();
     require(pa && pb && pa->pcmSamples==pb->pcmSamples && pa->firstSampleFrame==pb->firstSampleFrame,"FIFO producer sample stream mismatch");
     audibleSamples|=std::any_of(pa->pcmSamples.begin(),pa->pcmSamples.end(),[](int16_t v){return v!=0;});
     aa.appendRecentPcm(pa->pcmSamples,last);ab.appendRecentPcm(pb->pcmSamples,last);
     while(true){
       const bool xa=aa.produceReadyOutputBlock(),xb=ab.produceReadyOutputBlock();require(xa==xb,"FIFO readiness mismatch");if(!xa)break;
       const auto depth=ab.transportStats().readyQueueDepth;
       require(aa.transportStats().readyQueueDepth==1 && depth==1,"prepared FIFO occupancy mismatch");
       occupancyLow=std::min(occupancyLow,depth);occupancyHigh=std::max(occupancyHigh,depth);++occupancySamples;
       aa.drainReadyOutput(x);
       const auto start=Tests::Perf::Clock::now();ab.drainReadyOutput(y);
       const auto elapsed=std::chrono::duration_cast<Tests::Perf::Nanoseconds>(Tests::Perf::Clock::now()-start);ab.noteDrainCallbackDuration(elapsed);durations.push_back(elapsed.count());
       require(x==y,"FIFO device-rate output mismatch");
       require(!aa.transportStats().readyQueueDepth && !ab.transportStats().readyQueueDepth,"FIFO drain did not consume prepared chunk");
     }
   }
   require(a.deterministicStateFingerprint()==b.deterministicStateFingerprint(),"FIFO producer device state mismatch");
 }
 const auto stats=ab.transportStats();const auto engine=ab.engine().stats();
 require(haltedAdvanceStats.bulkCalls && audibleSamples && durations.size()>100,"FIFO candidate/tone/callback coverage missing");
 require(occupancySamples==durations.size() && occupancyLow==1 && occupancyHigh==1 && stats.readyQueueHighWaterChunks==1,"FIFO occupancy changed");
 require(!stats.underrunCount && !stats.silenceSamplesFilled && !stats.sourceQueueOverruns && !stats.droppedReadyBlocks && !engine.overrunDropCount,"FIFO underrun/overrun");
 const auto p99=Tests::Perf::percentile(durations,.99);
 std::cout<<"halted FIFO core="<<core<<" bulk_calls="<<haltedAdvanceStats.bulkCalls<<" callbacks="<<durations.size()<<" drain_occupancy_low="<<occupancyLow<<" drain_occupancy_high="<<occupancyHigh<<" ready_high="<<stats.readyQueueHighWaterChunks<<" underruns="<<stats.underrunCount<<" overruns="<<engine.overrunDropCount<<" callback_p99_ns="<<p99<<" limit_ns=50000\n";
 require(!Tests::Perf::kEnforceWallClockThresholds || p99<50000,"prepared FIFO callback latency gate failed");
}
}
int main()try{check<GB::GameBoyMachine>("gameboy");check<GameGearMachine>("gamegear");return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
