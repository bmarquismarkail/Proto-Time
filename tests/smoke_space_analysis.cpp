#include "space/Session.hpp"
#include "fixtures/space/AnalysisFixture.hpp"
#include <iostream>
using namespace BMMQ::Space;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"expected rejection");}
const Json& routineAt(const Json& p,unsigned address){for(auto& r:p["analysis"]["routines"])if(p["instructions"][r["entry"].get<std::string>()]["address"]==address)return r;throw std::runtime_error("routine not found");}
std::string instructionAt(const Json& p,unsigned address){for(auto it=p["instructions"].begin();it!=p["instructions"].end();++it)if(it.value()["address"]==address)return it.key();throw std::runtime_error("instruction not found");}
}
int main(){try{
    auto rom=spaceAnalysisFixture();GB::GameBoyMachine baseline,snapshot;baseline.loadRom(rom);snapshot.loadRom(rom);
    Session live(baseline,rom),shadow(snapshot,rom);shadow.executionMode("snapshot");live.input(1);shadow.input(1);
    for(int i=0;i<500;++i){live.step();shadow.step();check(baseline.deterministicStateFingerprint()==snapshot.deterministicStateFingerprint(),"fixture instruction parity failed");}
    auto before=baseline.deterministicStateFingerprint();auto doc=live.document();auto analyzed=analyzeProject(doc);
    check(analyzed==analyzeProject(doc),"analysis is not deterministic");check(!analyzed["analysis"]["incomplete"].get<bool>(),"fixture analysis incomplete");
    check(analyzed["analysis"]["dataTransfers"].size()>=4,"tile transfer paths missing");
    bool audio=false,tiles=false,poll=false;
    for(auto& f:analyzed["analysis"]["findings"]){audio=audio||f["role"]=="audio.configuration";tiles=tiles||f["role"]=="graphics.tile-data-output";poll=poll||f["role"]=="input.polling";check(f["role"]!="timer.configuration","device retirement mislabeled as CPU purpose");}
    check(audio&&tiles&&poll,"known hardware roles missing");
    size_t audioWrites=0;
    for(auto& d:doc["dependencies"])if(d["location"]=="m:"+decimal(location(5,0,0xff12))&&d["origin"]=="cpu"&&d["access"]=="write"&&d["accepted"]==true)++audioWrites;
    check(audioWrites==1,"CPU MMIO write evidence duplicated or missing");
    auto& left=routineAt(analyzed,0x280);auto& right=routineAt(analyzed,0x290);
    check(!left["sharedInstructions"].empty()&&!right["sharedInstructions"].empty(),"shared tail lost");
    auto& recursive=routineAt(analyzed,0x2b0);check(std::find(recursive["callees"].begin(),recursive["callees"].end(),recursive["id"])!=recursive["callees"].end(),"recursive call missing");
    routineAt(analyzed,0x40);routineAt(analyzed,0x2d0);
    check(std::none_of(analyzed["analysis"]["routines"].begin(),analyzed["analysis"]["routines"].end(),[&](auto& r){return analyzed["instructions"][r["entry"].template get<std::string>()]["address"]==0x2e0;}),"not-taken call invented callee");
    auto frozen=live.analyze();check(baseline.deterministicStateFingerprint()==before,"analysis changed machine state");
    auto identity=live.query({{"type","summary"}})["analysisId"];for(int i=0;i<4;++i)live.step();check(live.query({{"type","summary"}})["analysisId"]==identity,"queries followed live revision");
    auto replacement=live.analyze();check(replacement["id"]!=identity,"explicit analyze did not replace revision");
    auto f=analyzed["analysis"]["findings"][0];auto note=Json{{"kind","correction"},{"text","Fixture-specific interpretation"},{"finding",f["id"]}};
    annotateDocument(analyzed,f["instruction"].get<std::string>(),note);Project::validate(analyzed);check(analyzed["analysis"]["findings"][0]==f,"correction discarded inference");
    auto page=queryAnalysis(analyzed,{{"type","dependencies"},{"limit",1}});check(page["truncated"]==true&&page["nextOffset"]==1,"query pagination broken");
    auto path=queryAnalysis(analyzed,{{"type","dependency_path"},{"id",analyzed["analysis"]["dataTransfers"][0]["target"]},{"depth",16}});check(path["items"].size()>2,"supplier path query empty");
    rejects([&]{queryAnalysis(analyzed,{{"type","summary"},{"limit",1025}});});rejects([&]{queryAnalysis(analyzed,{{"type","summary"},{"analysisId","wrong"}});});
    auto corrupt=analyzed;corrupt["analysis"]["findings"][0]["dependencies"].push_back("missing");rejects([&]{Project::validate(corrupt);});
    corrupt=analyzed;corrupt["analysis"]["findings"][0]["branches"].push_back("missing");rejects([&]{Project::validate(corrupt);});
    corrupt=analyzed;corrupt["dependencies"][0]["value"]=7;rejects([&]{Project::validate(corrupt);});
    auto rejected=doc;for(auto& d:rejected["dependencies"])if(d["location"].get<std::string>().starts_with("m:")&&(counter(Json(d["location"].get<std::string>().substr(2)))>>32)==4)d["accepted"]=false;
    rejected=analyzeProject(rejected);check(std::none_of(rejected["analysis"]["findings"].begin(),rejected["analysis"]["findings"].end(),[](auto& f){return f["role"]=="graphics.tile-data-output";}),"rejected writes inferred output");
    auto exhausted=analyzeProject(doc,1);check(exhausted["analysis"]["incomplete"]==true&&exhausted["analysis"]["routines"].empty(),"resource loss silent");
    auto legacy=doc;legacy.erase("transfers");for(auto& d:legacy["dependencies"])d.erase("origin");legacy=analyzeProject(legacy);
    check(legacy["analysis"]["findings"].empty(),"ambiguous legacy accesses inferred CPU purpose");
    baseline.executionMemory().file.findRegister("PC")->reg->value=0x260;
    auto target=instructionAt(doc,0x260);before=baseline.deterministicStateFingerprint();auto stopped=live.runUntil({{"instruction",target},{"count",10}});
    check(stopped["reason"]=="target_reached"&&stopped["count"]=="0"&&baseline.deterministicStateFingerprint()==before,"instruction target did not stop before effects");
    auto event=live.runUntil({{"event",{{"category","audio.channel"},{"access","write"}}},{"count",10}});
    check(event["reason"]=="event_observed"&&event["event"]["address"]==0xff12,"hardware target did not stop after CPU effect");
    baseline.executionMemory().file.findRegister("PC")->reg->value=0x300;
    stopped=live.runUntil({{"event",{{"category","graphics.tile-data"},{"access","write"}}},{"count",3}});check(stopped["reason"]=="step_limit"&&stopped["count"]=="3","run_until bound failed");
    shadow.analyze();snapshot.executionMemory().file.findRegister("PC")->reg->value=0x260;
    before=snapshot.deterministicStateFingerprint();stopped=shadow.runUntil({{"instruction",target},{"count",10}});
    check(stopped["reason"]=="target_reached"&&stopped["count"]=="0"&&snapshot.deterministicStateFingerprint()==before,"snapshot target did not stop before effects");
    snapshot.executionMemory().file.findRegister("PC")->reg->value=0x8000;
    stopped=shadow.runUntil({{"event",{{"category","audio.channel"},{"access","write"}}},{"count",3}});check(stopped["reason"]=="execution_pause"&&stopped["count"]=="0","snapshot pause lost");
    shadow.executionMode("baseline");shadow.step();
    auto& map=dynamic_cast<GB::GameBoyMemoryMap&>(snapshot.executionMemory().backingStore());map.analysisCapture->stop();
    before=snapshot.deterministicStateFingerprint();stopped=shadow.runUntil({{"event",{{"address",0xff12},{"access","write"}}},{"count",3}});
    check(stopped["reason"]=="evidence_loss"&&stopped["count"]=="0"&&snapshot.deterministicStateFingerprint()==before,"evidence loss resumed execution");
    // A transform terminates the load-only ancestry even when its value is unchanged.
    auto transform=spaceFixture();const uint8_t prog[]={0x21,0,0xc0,0x7e,0xf6,0,0xe0,0x12,0xc3,0x80,1};std::copy(std::begin(prog),std::end(prog),transform.begin()+0x150);
    GB::GameBoyMachine transformed;transformed.loadRom(transform);Session ts(transformed,transform);for(int i=0;i<20;++i)ts.step();auto ta=analyzeProject(ts.document());
    check(std::none_of(ta["analysis"]["dataTransfers"].begin(),ta["analysis"]["dataTransfers"].end(),[&](auto& t){auto id=t["target"].template get<std::string>();for(auto& d:ta["dependencies"])if(d["id"]==id)return d["location"]=="m:"+decimal(location(5,0,0xff12));return false;}),"equal values crossed unsupported transformation");
    std::cout<<"purpose, loops, frozen queries, suppliers and bounded exploration passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
