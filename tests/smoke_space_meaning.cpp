#include "space/Session.hpp"
#include "space/Meaning.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "fixtures/space/Fixture.hpp"
#include <iostream>
using namespace BMMQ::Space;
namespace {
void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
template<class F>void reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"invalid meaning metadata accepted");}
std::string textHash(const std::string& text){return digest(std::span(reinterpret_cast<const uint8_t*>(text.data()),text.size()));}
void test(bool gear){
    auto image=spaceFixture();std::unique_ptr<BMMQ::Machine> machine;
    if(gear){image.assign(32768,0);image[0]=0x3e;image[1]=0x80;image[2]=0xd3;image[3]=0x7f;image[4]=0xc3;machine=std::make_unique<BMMQ::GameGearMachine>();}
    else machine=std::make_unique<GB::GameBoyMachine>();
    machine->loadRom(image);Session session(*machine,image);for(unsigned i=0;i<12;++i)session.step();auto project=session.document();
    const auto core=project["core"];auto node=project["instructions"].begin();auto instruction=node.key();
    const std::string source="Entry: ; reviewed fixture source\n";
    Json symbols={{"schemaVersion",1},{"core",core},{"romSha256",digest(image)},
        {"sources",Json::array({{{"path","fixture.asm"},{"sha256",textHash(source)},{"content",source}}})},
        {"symbols",Json::array({{{"name","Entry"},{"location",node.value()["location"]},{"source","fixture.asm"},{"line",1}}})}};
    auto original=project;auto wrong=symbols;wrong["core"]=gear?"gameboy":"gamegear";reject([&]{importSymbols(project,wrong,image);});require(project==original,"rejected import mutated capture");
    wrong=symbols;wrong["sources"][0]["content"]="changed";reject([&]{importSymbols(project,wrong,image);});
    importSymbols(project,symbols,image);auto imported=project;importSymbols(project,symbols,image);require(project==imported,"non-idempotent symbol import");
    auto linked=project["dependencies"][0]["id"];
    Json claim={{"instruction",instruction},{"purpose","fixture output setup"},{"status","inferred"},{"dependencies",Json::array({linked})}};
    addPurposeClaim(project,claim);auto inferred=project;claim["status"]="reviewed";reject([&]{addPurposeClaim(project,claim);});require(project==inferred,"unreviewed claim published");
    claim["reviewer"]="fixture test reviewer";claim["rationale"]="Known fixture contract with linked captured dependencies";addPurposeClaim(project,claim);
    auto analyzed=analyzeProject(project);require(analyzed["analysis"]["purposeFindings"].size()==2,"purpose claims lost");
    require(queryAnalysis(analyzed,{{"type","purposes"}})["items"].size()==2,"purpose query failed");
    require(queryAnalysis(analyzed,{{"type","symbols"}})["items"].size()==1,"symbol query failed");
    auto changed=analyzed;changed["analysis"]["purposeFindings"][0]["purpose"]="forged";reject([&]{Project::validate(changed);});
    Project merged(digest(image),Project::defaultBudget,core.get<std::string>());merged.merge(project);Project::validate(merged.document());
    require(merged.document()["symbolImports"]==project["symbolImports"]&&merged.document()["purposeClaims"]==project["purposeClaims"],"meaning metadata lost on merge");
}
}
int main(){try{test(false);test(true);std::cout<<"ROM/source binding and evidence-linked purpose claims passed on both cores\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
