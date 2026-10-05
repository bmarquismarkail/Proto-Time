#include "Session.hpp"
#include "Porting.hpp"
#include <iostream>
#include <fstream>
#include <set>
#include <future>
namespace {
using namespace BMMQ::Space;
std::vector<uint8_t> readRom(const std::filesystem::path& path){
    if(std::filesystem::file_size(path)>64u*1024u*1024u)throw std::invalid_argument("ROM too large");
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot read ROM");return {(std::istreambuf_iterator<char>(f)),{}};
}
}
int main(int argc,char** argv){
    using namespace BMMQ::Space;
    try {
        if(argc<2){std::cout<<"time-space explore --rom ROM --project PROJECT [--commands JSONL] [--execution baseline|snapshot]\n"
            "time-space analyze --project PROJECT --output ANALYZED\n"
            "time-space query --project ANALYZED --commands JSONL\n"
            "time-space merge --project PROJECT --input OTHER\n"
            "time-space export --project PROJECT --html VIEWER\n"
            "time-space port init|import|check|record-verification --project PROJECT --rom GB --target GG [--input JSON]\n";return 0;}
        std::map<std::string,std::string> args;
        std::string mode=argv[1],portOp;int first=2;if(mode=="port"){if(argc<3)throw std::invalid_argument("missing port operation");portOp=argv[2];first=3;}
        for(int i=first;i<argc;i+=2){if(i+1>=argc)throw std::invalid_argument("missing option value");std::string key=argv[i];
            if(key!="--rom"&&key!="--project"&&key!="--commands"&&key!="--input"&&key!="--html"&&key!="--execution"&&key!="--output"&&key!="--target")throw std::invalid_argument("unknown option "+key);
            if(!args.emplace(key,argv[i+1]).second)throw std::invalid_argument("duplicate option "+key);}
        auto require=[&](std::string key){if(!args.contains(key)||args[key].empty())throw std::invalid_argument("missing "+key);return std::filesystem::path(args[key]);};
        auto projectPath=require("--project");
        if(mode=="port"){
            Json document;if(portOp=="init"&&!std::filesystem::exists(projectPath)){auto rom=readRom(require("--rom"));document=Project(digest(rom)).document();}else document=Project::read(projectPath);
            Project::validate(document);
            if(portOp=="init")attachPorting(document,Project::read(require("--input")));
            else if(portOp=="import"){auto incoming=Project::read(require("--input"));auto ledger=incoming.contains("porting")?incoming["porting"]:incoming;attachPorting(document,document.contains("porting")?mergePorting(document["porting"],ledger):ledger);}
            else if(portOp=="record-verification")recordVerification(document,Project::read(require("--input")));
            else if(portOp!="check")throw std::invalid_argument("unknown port operation");
            verifyPortArtifacts(document,require("--rom"),require("--target"));auto check=checkPorting(document);
            if(portOp!="check"){Project::write(projectPath,document);}
            std::cout<<Json({{"ok",true},{"result",check}}).dump()<<'\n';return 0;
        }
        if(mode=="analyze"){auto frozen=Project::read(projectPath);auto task=std::async(std::launch::async,[p=std::move(frozen)]()mutable{return analyzeProject(std::move(p));});Project::write(require("--output"),task.get());return 0;}
        if(mode=="query"){auto frozen=Project::read(projectPath);Project::validate(frozen);std::ifstream input(require("--commands"));if(!input)throw std::runtime_error("cannot read commands");std::string line;
            while(std::getline(input,line)){if(line.empty())continue;if(line.size()>65536)throw std::invalid_argument("command exceeds 64 KiB");auto request=Json::parse(line);std::cout<<Json({{"ok",true},{"result",queryAnalysis(frozen,request)}}).dump()<<'\n';}return 0;}
        if(mode=="export"){auto p=Project::load(projectPath);exportHtml(p,require("--html"));return 0;}
        if(mode=="merge"){auto p=Project::load(projectPath);p.merge(Project::read(require("--input")));p.save(projectPath);return 0;}
        if(mode!="explore")throw std::invalid_argument("unknown subcommand");
        auto rom=readRom(require("--rom"));GB::GameBoyMachine machine;machine.loadRom(rom);Session session(machine,rom,projectPath);session.executionMode(args.contains("--execution")?args["--execution"]:"baseline");
        std::ifstream file;std::istream* commands=&std::cin;
        if(args.contains("--commands")){file.open(args["--commands"]);if(!file)throw std::runtime_error("cannot read commands");commands=&file;}
        std::string line;bool failed=false;
        while(std::getline(*commands,line)){
            if(line.empty())continue;
            try {
                if(line.size()>65536)throw std::invalid_argument("command exceeds 64 KiB");
                auto command=Json::parse(line);auto op=command.at("op").get<std::string>();Json result={{"ok",true},{"op",op}};
                if(op=="step"||op=="run"){
                    auto count=command.value("count",Json(1));if(!count.is_number_integer()||count<1||count>1000000)throw std::invalid_argument("step count must be 1–1000000");
                    uint64_t n=count.get<uint64_t>();
                    uint64_t executed=0;
                    try{for(;executed<n;++executed){session.step();if(session.executionStatus()["activeMode"]=="paused"){++executed;break;}}}
                    catch(const ExecutionPaused& e){result["paused"]=true;result["pauseReason"]=e.what();}
                    result["execution"]=session.executionStatus();if(result["execution"]["activeMode"]=="paused")result["paused"]=true;
                    session.flush();n=executed;
                    result["fingerprint"]=machine.deterministicStateFingerprint();result["count"]=decimal(n);
                } else if(op=="input") {auto mask=command.at("mask");if(!mask.is_number_integer()||mask<0||mask>255)throw std::invalid_argument("input mask must be 0–255");session.input(mask.get<uint8_t>());}
                else if(op=="execution"){session.executionMode(command.at("mode").get<std::string>());result["execution"]=session.executionStatus();}
                else if(op=="analyze"){auto analysis=session.analyze();result["analysis"]=analysis;if(command.contains("path"))session.saveAnalysis(command["path"].get<std::string>());}
                else if(op=="query")result["result"]=session.query(command);
                else if(op=="annotate")session.annotate(command.at("instruction").get<std::string>(),command.at("annotation"));
                else if(op=="run_until")result["result"]=session.runUntil(command);
                else if(op=="checkpoint")session.checkpoint(command.at("path").get<std::string>());
                else if(op=="restore")session.restore(command.at("path").get<std::string>());
                else if(op=="export") {auto path=command.value("path",projectPath.string());if(command.value("analyzed",false))session.saveAnalysis(path);else session.save(path);if(command.contains("html")){auto p=Project::load(path);exportHtml(p,command["html"].get<std::string>());}}
                else if(op=="status"){auto d=session.document();result["revision"]=d["revision"];result["blocks"]=d["blocks"].size();result["gaps"]=d["gaps"];result["history"]=d["history"];result["execution"]=session.executionStatus();result["fingerprint"]=machine.deterministicStateFingerprint();}
                else if(op=="quit"){session.save(projectPath);std::cout<<result.dump()<<'\n';break;}
                else throw std::invalid_argument("unknown command "+op);
                std::cout<<result.dump()<<'\n'<<std::flush;
            }catch(const std::exception& e){failed=true;std::cout<<Json({{"ok",false},{"error",e.what()}}).dump()<<'\n'<<std::flush;if(commands==&file)break;}
        }
        session.save(projectPath);return failed?1:0;
    }catch(const std::exception& e){std::cerr<<Json({{"ok",false},{"error",e.what()}}).dump()<<'\n';return 1;}
}
