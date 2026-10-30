#include "Authoring.hpp"
#include <fstream>
#include <iostream>
using namespace BMMQ::VisualAuthoring;
namespace {
std::string read(const std::filesystem::path& p,std::size_t limit) {
    const auto size=std::filesystem::file_size(p);
    if(!size || size>limit)throw std::invalid_argument("authoring input budget");
    std::string bytes(size,'\0');std::ifstream f(p,std::ios::binary);
    if(!f.read(bytes.data(),size))throw std::runtime_error("authoring input read failed");
    return bytes;
}
}
int main(int argc,char** argv) {
    try {
        std::filesystem::path capture,source,assets,output;std::string language="file";
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];if(i+1==argc)throw std::invalid_argument("option requires a value");
            if(arg=="--capture")capture=argv[++i];else if(arg=="--source")source=argv[++i];
            else if(arg=="--assets")assets=argv[++i];else if(arg=="--output")output=argv[++i];
            else if(arg=="--language")language=argv[++i];else throw std::invalid_argument("unknown option");
        }
        if(capture.empty() || source.empty() || assets.empty() || output.empty())throw std::invalid_argument(
            "usage: time-visual-author --capture JSON --source RECIPE --assets DIRECTORY --output NEW_DIRECTORY [--language file|lua|python|javascript]");
        const auto observed=Capture::read(parse(read(capture,4*1024*1024),4*1024*1024));
        auto program=read(source,65536);Prepared prepared;
        if(language=="file")prepared=Engine::prepare(observed,parse(program,65536),assets);
        else {
            BMMQ::Script::Language selected;
            if(language=="lua")selected=BMMQ::Script::Language::Lua;
            else if(language=="python")selected=BMMQ::Script::Language::Python;
            else if(language=="javascript")selected=BMMQ::Script::Language::JavaScript;
            else throw std::invalid_argument("unsupported authoring language");
            prepared=Engine::script(observed,selected,program,assets);
        }
        Engine::publish(prepared,observed.binding,output);
        std::cout<<Json{{"ok",true},{"core",observed.binding.core},{"romSha256",observed.binding.romSha256},
                       {"manifest",(output/"manifest.json").string()},{"annotations",(output/"annotations.json").string()}}.dump()<<'\n';return 0;
    }catch(const std::exception& e) {std::cerr<<"time-visual-author: "<<e.what()<<'\n';return 1;}
}
