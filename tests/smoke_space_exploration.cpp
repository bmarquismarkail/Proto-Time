#include "space/Session.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include "fixtures/space/Fixture.hpp"
#include <iostream>
#include <chrono>
using namespace BMMQ::Space;
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
Json explore(bool gear){
    auto image=spaceFixture();std::unique_ptr<BMMQ::Machine> machine;
    if(gear){image.assign(32768,0);image[0]=0xc3;machine=std::make_unique<BMMQ::GameGearMachine>();}
    else machine=std::make_unique<GB::GameBoyMachine>();
    machine->loadRom(image);Session session(*machine,image);
    auto before=session.fingerprint();bool rejected=false;
    try{session.autoExplore({{"maxSteps",0}});}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&before==session.fingerprint(),"invalid exploration request mutated state");
    auto result=session.autoExplore({{"maxSteps",64},{"maxAttempts",8},{"stagnantAttempts",2},{"stepsPerAttempt",8},{"masks",{0,1}},
        {"scenarios",Json::array({{{"id","first"},{"actions",Json::array({{{"mask",0},{"steps",8}}})}}})}});
    require(result["core"]==(gear?"gamegear":"gameboy"),"wrong exploration core");
    require(counter(result["steps"])<=64&&result["attempts"].size()<=8,"exploration limits exceeded");
    require(result["attempts"].size()>1,"checkpoint branches not attempted");
    require(result["stuck"]["missingScenarios"].empty(),"scenario schedule not accounted");
    require(session.document()["gaps"].empty(),"exploration lost evidence");
    auto path=std::filesystem::temp_directory_path()/("time-space-gap-"+decimal(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
    Project incomplete(digest(image),Project::defaultBudget,gear?"gamegear":"gameboy");incomplete.gap("test evidence loss");incomplete.save(path);
    auto candidate=makeCoreAdapter(*machine)->candidate(image);Session lost(*candidate,image,path);auto lostBefore=lost.fingerprint();bool paused=false;
    try{lost.autoExplore({{"maxSteps",8}});}catch(const ExecutionPaused&){paused=true;}
    require(paused&&lostBefore==lost.fingerprint(),"exploration stepped with incomplete initial evidence");
    // Nondeterministic project history names are intentionally absent from the report.
    return result;
}
}
int main(){try{for(bool gear:{false,true})require(explore(gear)==explore(gear),"exploration report is not reproducible");
    std::cout<<"SPACE bounded checkpoint exploration passed on both cores\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
