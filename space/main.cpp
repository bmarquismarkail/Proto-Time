#include "Session.hpp"
#include <iostream>
#include <fstream>
#include <set>
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
        if(argc<2){std::cout<<"time-space explore --rom ROM --project PROJECT [--commands JSONL]\n"
            "time-space merge --project PROJECT --input OTHER\n"
            "time-space export --project PROJECT --html VIEWER\n";return 0;}
        std::map<std::string,std::string> args;
        for(int i=2;i<argc;i+=2){if(i+1>=argc)throw std::invalid_argument("missing option value");std::string key=argv[i];
            if(key!="--rom"&&key!="--project"&&key!="--commands"&&key!="--input"&&key!="--html")throw std::invalid_argument("unknown option "+key);
            if(!args.emplace(key,argv[i+1]).second)throw std::invalid_argument("duplicate option "+key);}
        auto require=[&](std::string key){if(!args.contains(key)||args[key].empty())throw std::invalid_argument("missing "+key);return std::filesystem::path(args[key]);};
        std::string mode=argv[1];auto projectPath=require("--project");
        if(mode=="export"){auto p=Project::load(projectPath);exportHtml(p,require("--html"));return 0;}
        if(mode=="merge"){auto p=Project::load(projectPath);p.merge(Project::read(require("--input")));p.save(projectPath);return 0;}
        if(mode!="explore")throw std::invalid_argument("unknown subcommand");
        auto rom=readRom(require("--rom"));GB::GameBoyMachine machine;machine.loadRom(rom);Session session(machine,rom,projectPath);
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
                    for(uint64_t i=0;i<n;++i){machine.step();if((i&31)==31)session.flush();}session.flush();
                    result["fingerprint"]=machine.deterministicStateFingerprint();result["count"]=decimal(n);
                } else if(op=="input") {auto mask=command.at("mask");if(!mask.is_number_integer()||mask<0||mask>255)throw std::invalid_argument("input mask must be 0–255");session.input(mask.get<uint8_t>());}
                else if(op=="checkpoint")session.checkpoint(command.at("path").get<std::string>());
                else if(op=="restore")session.restore(command.at("path").get<std::string>());
                else if(op=="export") {auto path=command.value("path",projectPath.string());session.save(path);if(command.contains("html")){auto p=Project::load(path);exportHtml(p,command["html"].get<std::string>());}}
                else if(op=="status"){auto d=session.document();result["revision"]=d["revision"];result["blocks"]=d["blocks"].size();result["gaps"]=d["gaps"];result["history"]=d["history"];result["fingerprint"]=machine.deterministicStateFingerprint();}
                else if(op=="quit"){session.save(projectPath);std::cout<<result.dump()<<'\n';break;}
                else throw std::invalid_argument("unknown command "+op);
                std::cout<<result.dump()<<'\n'<<std::flush;
            }catch(const std::exception& e){failed=true;std::cout<<Json({{"ok",false},{"error",e.what()}}).dump()<<'\n'<<std::flush;if(commands==&file)break;}
        }
        session.save(projectPath);return failed?1:0;
    }catch(const std::exception& e){std::cerr<<Json({{"ok",false},{"error",e.what()}}).dump()<<'\n';return 1;}
}
