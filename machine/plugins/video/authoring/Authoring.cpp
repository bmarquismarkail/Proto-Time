#include "Authoring.hpp"
#include "machine/PngDecode.hpp"
#include "machine/VisualPackManifest.hpp"
#include "machine/VisualPackLimits.hpp"
#include <algorithm>
#include <fstream>
#include <fcntl.h>
#include <set>
#include <sys/syscall.h>
#include <unistd.h>

namespace BMMQ::VisualAuthoring {
namespace {
void require(bool ok, const char* message) { if(!ok) throw std::invalid_argument(message); }
std::string text(const Json& j, const char* key, std::size_t max) {
    const auto s=j.at(key).get<std::string>();
    require(!s.empty() && s.size()<=max && s.find('\0')==s.npos,"authoring text budget"); return s;
}
std::string hash(const Json& j, const char* key, std::size_t length) {
    auto s=text(j,key,length);
    require(s.size()==length && s.find_first_not_of("0123456789abcdef")==s.npos,"invalid hash");return s;
}
std::string identity(const Json& j,const char* key) {
    auto s=text(j,key,20);require(s.find_first_not_of("0123456789")==s.npos,"invalid capture identity");
    std::size_t end{};std::stoull(s,&end);require(end==s.size(),"invalid capture identity");return s;
}
std::string resourceKey(const Json& j) {
    auto kind=text(j,"kind",32); require(kind=="Tile" || kind=="Sprite" || kind=="BackgroundTile","unknown visual resource kind");
    auto source=text(j,"sourceHash",18);
    if(source.starts_with("0x"))source.erase(0,2);
    require(source.size()==16 && source.find_first_not_of("0123456789abcdef")==source.npos,"invalid resource hash");
    return kind+":0x"+source;
}
void write(const std::filesystem::path& p,std::span<const std::uint8_t> data) {
    std::ofstream f(p,std::ios::binary);if(!f.write(reinterpret_cast<const char*>(data.data()),data.size()) || !f.flush())
        throw std::runtime_error("authoring artifact write failed");
}
void write(const std::filesystem::path& p,const std::string& s) {
    write(p,{reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});
}
}
Json parse(std::string_view source,std::size_t maximum) {
    require(!source.empty() && source.size()<=maximum,"authoring JSON budget");
    return Json::parse(source,[](int depth,Json::parse_event_t,Json&) {
        require(depth<=32,"authoring JSON nesting budget");return true;
    });
}
Capture Capture::read(const Json& j) {
    require(j.at("schemaVersion")==1,"unsupported authoring capture version");
    Capture c;c.binding={text(j,"core",16),hash(j,"romSha256",64),identity(j,"videoSequence"),identity(j,"videoGeneration")};
    require(c.binding.core=="gameboy" || c.binding.core=="gamegear","unsupported authoring core");
    c.resources=j.at("resources");require(c.resources.is_array() && c.resources.size()<=23040,"resource inventory budget");
    for(const auto& r:c.resources)resourceKey(r);
    c.cpu.core=c.binding.core;c.cpu.state.generation=std::stoull(c.binding.generation);
    if(j.contains("cpu")) {
        require(identity(j.at("cpu"),"generation")==c.binding.generation &&
                identity(j,"sequence")==c.binding.videoSequence,"CPU and video observations must share one capture");
        const auto state=j.at("cpu").at("state").get<unsigned>();require(state<=3,"invalid captured CPU state");
        c.cpu.state.state=static_cast<Debug::State>(state);
        const auto& regs=j.at("cpu").at("registers");require(regs.is_array() && regs.size()==20,"register extent");
        for(unsigned i=0;i<20;++i) {require(regs[i].is_number_unsigned() && regs[i].get<std::uint64_t>()<=65535,"register range");c.cpu.state.registers[i]=regs[i].get<std::uint16_t>();}
    }
    return c;
}
Prepared Engine::prepare(const Capture& c,const Json& recipe,const std::filesystem::path& assetRoot) {
    Capture::read({{"schemaVersion",1},{"core",c.binding.core},{"romSha256",c.binding.romSha256},
                   {"videoSequence",c.binding.videoSequence},{"videoGeneration",c.binding.generation},{"resources",c.resources}});
    require(recipe.dump().size()<=65536,"recipe budget");
    require(recipe.at("schemaVersion")==1 && recipe.at("core")==c.binding.core && recipe.at("romSha256")==c.binding.romSha256 &&
            identity(recipe,"videoSequence")==c.binding.videoSequence && identity(recipe,"generation")==c.binding.generation,"recipe binding rejected");
    Prepared p;p.binding_=c.binding;
    p.manifest_={{"schemaVersion",1},{"id",text(recipe,"id",64)},{"name",text(recipe,"name",128)},{"target",c.binding.core},{"rules",Json::array()}};
    p.annotations_={{"schemaVersion",1},{"core",c.binding.core},{"romSha256",c.binding.romSha256},{"videoSequence",c.binding.videoSequence},{"generation",c.binding.generation},{"labels",Json::array()}};
    const auto& items=recipe.at("resources");require(items.is_array() && items.size()<=256,"authoring rule budget");
    std::set<std::string> observed, used;for(const auto& r:c.resources)observed.insert(resourceKey(r));
    const auto root=std::filesystem::canonical(assetRoot);std::size_t assetBytes=0;
    for(const auto& item:items) {
        const auto key=resourceKey(item);require(observed.contains(key) && used.insert(key).second,"unobserved or duplicate resource");
        const auto normalizedHash=key.substr(key.find(':')+1);
        if(item.contains("label"))p.annotations_["labels"].push_back({{"kind",item.at("kind")},{"sourceHash",normalizedHash},
            {"label",text(item,"label",128)},{"reviewer",text(item,"reviewer",128)},{"evidence",text(item,"evidence",1024)},{"status","reviewed-annotation"}});
        if(!item.contains("replace"))continue;
        Json replacement=item.at("replace");require(replacement.is_object(),"replacement object required");
        for(auto it=replacement.begin();it!=replacement.end();++it)
            require(std::set<std::string>{"image","layers","animation","palette","slicing","transform","effects","script","scalePolicy","filterPolicy","anchor"}.contains(it.key()),"unsupported replacement field");
        if(replacement.contains("transform")) {
            const auto& transform=replacement.at("transform");require(transform.is_object(),"transform object required");
            for(auto it=transform.begin();it!=transform.end();++it) {
                if(it.key()=="flipX" || it.key()=="flipY")require(it.value().is_boolean(),"transform flag must be boolean");
                else if(it.key()=="rotate")require(it.value().is_number_integer() && it.value().get<std::int64_t>()>=0 &&
                    it.value().get<std::int64_t>()<=270 && it.value().get<std::int64_t>()%90==0,"invalid rotation");
                else require(false,"unsupported transform field");
            }
        }
        if(replacement.contains("animation")) {
            const auto& duration=replacement.at("animation").at("frameDuration");
            require(duration.is_number_integer() && duration.get<std::int64_t>()>0 && duration.get<std::int64_t>()<=60000,"invalid animation duration");
        }
        if(replacement.contains("slicing")) {
            const auto& slice=replacement.at("slicing");require(slice.is_object() && slice.size()==4,"invalid slice extent");
            for(const char* field:{"x","y","width","height"})require(slice.at(field).is_number_integer() &&
                slice.at(field).get<std::int64_t>()>=0 && slice.at(field).get<std::int64_t>()<=2048,"invalid slice value");
            require(slice.at("width").get<unsigned>() && slice.at("height").get<unsigned>(),"empty slice");
        }
        for(const auto* field:{"image","layers","animation"}) {
            if(!replacement.contains(field))continue;
            auto images=field==std::string("image")?Json::array({replacement.at(field)}):
                field==std::string("animation")?replacement.at(field).at("frames"):replacement.at(field);
            require(images.is_array() && !images.empty() && images.size()<=64,"replacement asset budget");
            Json rewritten=Json::array();
            for(const auto& image:images) {
                auto relative=std::filesystem::path(image.get<std::string>());require(!relative.empty() && !relative.is_absolute(),"relative asset required");
                auto path=std::filesystem::canonical(root/relative);auto inside=path.lexically_relative(root);
                require(!inside.empty() && *inside.begin()!=".." && !inside.is_absolute(),"asset escapes authoring root");
                const auto size=std::filesystem::file_size(path);
                require(size>0 && size<=VisualPackLimits::kMaxPngBytes && assetBytes+size<=32*1024*1024,"owned asset budget");
                Prepared::Asset asset{"asset-"+std::to_string(p.assets_.size())+".png",std::vector<std::uint8_t>(size)};
                std::ifstream f(path,std::ios::binary);require(bool(f.read(reinterpret_cast<char*>(asset.bytes.data()),size)),"asset read failed");
                const auto decoded=decodePngToRgba(asset.bytes);require(decoded.success,"invalid replacement PNG");
                if(replacement.contains("slicing")) {
                    const auto& slice=replacement.at("slicing");
                    require(slice.at("x").get<unsigned>()+slice.at("width").get<unsigned>()<=decoded.image.width &&
                            slice.at("y").get<unsigned>()+slice.at("height").get<unsigned>()<=decoded.image.height,"slice outside replacement image");
                }
                assetBytes+=size;rewritten.push_back(asset.name);p.assets_.push_back(std::move(asset));
            }
            if(field==std::string("animation"))replacement[field]["frames"]=rewritten;
            else replacement[field]=field==std::string("image")?rewritten[0]:rewritten;
        }
        p.manifest_["rules"].push_back({{"match",{{"machineId",c.binding.core},{"kind",item.at("kind")},{"sourceHash",normalizedHash}}},{"replace",replacement}});
    }
    // Run the existing schema-1 parser in a private staging directory as the
    // authority for transforms, animations, layering, palettes and post effects.
    auto scratch=std::filesystem::temp_directory_path()/"time-authoring-XXXXXX";auto name=scratch.string();
    require(mkdtemp(name.data())!=nullptr,"authoring staging failed");scratch=name;
    try {
        for(const auto& a:p.assets_)write(scratch/a.name,a.bytes);
        write(scratch/"manifest.json",p.manifest_.dump());
        const auto parsed=loadVisualPackManifest(scratch/"manifest.json");
        require(parsed.manifest.has_value() && !parsed.invalidRulesSkipped && !parsed.missingReplacementImages &&
                parsed.manifest->rules.size()==p.manifest_["rules"].size(),"legacy manifest validation rejected recipe");
    }catch(...) {std::filesystem::remove_all(scratch);throw;}
    std::filesystem::remove_all(scratch);p.valid_=true;return p;
}
Prepared Engine::script(const Capture& c,Script::Language language,std::string_view source,const std::filesystem::path& root,Script::Limits limits) {
    require(c.cpu.core==c.binding.core && std::to_string(c.cpu.state.generation)==c.binding.generation,"script capture binding rejected");
    const auto result=Script::ScriptEngine::evaluate(language,source,c.cpu,{},limits);
    if(!result.success)throw std::invalid_argument("authoring script failed: "+result.error);
    require(!result.staged.editRegisters && !result.staged.byteCount,"authoring script requested guest mutation");
    return prepare(c,parse(result.report,65536),root);
}
void Engine::publish(const Prepared& p,const Binding& expected,const std::filesystem::path& output) {
    require(p.valid_ && p.binding_==expected,"stale or unprepared authoring publication");
    const auto destination=std::filesystem::absolute(output);require(!std::filesystem::exists(destination),"authoring output already exists");
    auto name=(destination.parent_path()/".time-authoring-XXXXXX").string();require(mkdtemp(name.data())!=nullptr,"authoring publication staging failed");
    const std::filesystem::path stage=name;
    try {
        for(const auto& a:p.assets_)write(stage/a.name,a.bytes);
        write(stage/"manifest.json",p.manifest_.dump(2)+"\n");write(stage/"annotations.json",p.annotations_.dump(2)+"\n");
        // Atomic publication refuses an output created by another author.
        if(syscall(SYS_renameat2,AT_FDCWD,stage.c_str(),AT_FDCWD,destination.c_str(),1))throw std::runtime_error("atomic authoring publication rejected");
    }catch(...) {std::filesystem::remove_all(stage);throw;}
}
}
