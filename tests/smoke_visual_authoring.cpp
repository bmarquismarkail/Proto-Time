#ifdef NDEBUG
#undef NDEBUG
#endif
#include "machine/plugins/video/authoring/Authoring.hpp"
#include "machine/VisualPackManifest.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gamegear/GameGearMachine.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <zlib.h>
using namespace BMMQ;
using namespace BMMQ::VisualAuthoring;
namespace {
void number(std::vector<std::uint8_t>& v,std::uint32_t n) {for(int shift:{24,16,8,0})v.push_back(n>>shift);}
void chunk(std::vector<std::uint8_t>& v,const char* tag,const std::vector<std::uint8_t>& bytes) {
    number(v,bytes.size());const auto start=v.size();for(unsigned i=0;i<4;++i)v.push_back(tag[i]);v.insert(v.end(),bytes.begin(),bytes.end());
    number(v,crc32(0,v.data()+start,4+bytes.size()));
}
void png(const std::filesystem::path& path) {
    std::vector<std::uint8_t> data{137,80,78,71,13,10,26,10},head;number(head,8);number(head,8);
    head.insert(head.end(),{8,6,0,0,0});chunk(data,"IHDR",head);
    std::vector<std::uint8_t> pixels(8*(1+8*4),0xff);for(unsigned y=0;y<8;++y)pixels[y*33]=0;
    uLongf size=compressBound(pixels.size());std::vector<std::uint8_t> compressed(size);assert(compress(compressed.data(),&size,pixels.data(),pixels.size())==Z_OK);
    compressed.resize(size);chunk(data,"IDAT",compressed);chunk(data,"IEND",{});
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());
}
template<class F> void rejected(F f) {bool failed=false;try {f();}catch(const std::exception&) {failed=true;}assert(failed);}
void verify(std::string core,const std::filesystem::path& root) {
    const auto assets=root/core;std::filesystem::create_directory(assets);png(assets/"tile.png");
    Json observed{{"schemaVersion",1},{"core",core},{"romSha256",std::string(64,'a')},{"videoSequence","9"},{"videoGeneration","2"},
                  {"resources",Json::array({{{"kind","Tile"},{"sourceHash","0123456789abcdef"}}})}};
    observed["sequence"]="9";observed["cpu"]={{"generation","2"},{"state",1},{"registers",std::array<std::uint16_t,20>{}}};
    const auto capture=Capture::read(observed);
    rejected([&]{Engine::publish(Prepared{},Binding{},root/(core+"-unprepared"));});
    auto mixed=observed;mixed["sequence"]="10";rejected([&]{Capture::read(mixed);});
    Json recipe{{"schemaVersion",1},{"id","authored"},{"name","Reviewed visual pack"},{"core",core},{"romSha256",std::string(64,'a')},
        {"videoSequence","9"},{"generation","2"},{"resources",Json::array({{{"kind","Tile"},{"sourceHash","0123456789abcdef"},
        {"label","hud_digit"},{"reviewer","fixture-review"},{"evidence","scenario hud-counter"},{"replace",{{"image","tile.png"},{"transform",{{"flipX",true},{"rotate",90}}}}}}})}};
    auto prepared=Engine::prepare(capture,recipe,assets);
    assert(prepared.annotations()["labels"][0]["status"]=="reviewed-annotation");
    auto stale=capture.binding;stale.generation="3";rejected([&]{Engine::publish(prepared,stale,root/(core+"-stale"));});
    assert(!std::filesystem::exists(root/(core+"-stale")));
    // Prepared assets own bytes; later author edits cannot change publication.
    std::ofstream(assets/"tile.png")<<"corrupt";
    const auto output=root/(core+"-output");Engine::publish(prepared,capture.binding,output);
    auto loaded=loadVisualPackManifest(output/"manifest.json");assert(loaded.manifest && loaded.manifest->rules.size()==1 && loaded.manifest->target==core);
    auto verifyMachine=[&](auto& machine) {
        machine.loadRom(std::vector<std::uint8_t>(32768));const auto before=machine.deterministicStateFingerprint();
        auto& service=machine.visualOverrideService();service.setEnabled(true);assert(service.loadPackManifest(output/"manifest.json"));
        VisualResourceDescriptor descriptor;descriptor.machineId=core;descriptor.kind=VisualResourceKind::Tile;
        descriptor.sourceHash=0x0123456789abcdef;descriptor.width=8;descriptor.height=8;
        descriptor.contentHash=hashVisualSourceBytes(std::array<std::uint8_t,64>{});
        descriptor.decodedFormat=core=="gameboy"?VisualPixelFormat::Indexed2:VisualPixelFormat::Indexed4;
        auto replacement=service.resolve(descriptor);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!replacement && std::chrono::steady_clock::now()<deadline) {service.pollBackgroundWork();std::this_thread::sleep_for(std::chrono::milliseconds(1));replacement=service.resolve(descriptor);}
        assert(replacement && replacement->packId=="authored" && replacement->transform.rotateDegrees==90);
        const auto& image=std::get<VisualReplacementImage>(replacement->payload);assert(image.width==8 && image.height==8 && image.argbPixels[0]==0xffffffff);
        assert(machine.deterministicStateFingerprint()==before);
    };
    if(core=="gameboy"){GB::GameBoyMachine machine;verifyMachine(machine);}else {GameGearMachine machine;verifyMachine(machine);}
    rejected([&]{Engine::publish(prepared,capture.binding,output);});
    rejected([&]{Engine::prepare(capture,recipe,assets);});png(assets/"tile.png");
    auto bad=recipe;bad["romSha256"]=std::string(64,'b');rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["core"]=core=="gameboy"?"gamegear":"gameboy";rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"][0]["sourceHash"]="ffffffffffffffff";rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"][0].erase("reviewer");rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"][0]["replace"]["image"]="../outside.png";png(root/"outside.png");rejected([&]{Engine::prepare(capture,bad,assets);});
    std::filesystem::create_symlink(root/"outside.png",assets/"escape.png");bad["resources"][0]["replace"]["image"]="escape.png";
    rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"].push_back(bad["resources"][0]);rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"][0]["replace"]["transform"]["rotate"]=13;rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"][0]["replace"]["transform"]["rotate"]=90.5;rejected([&]{Engine::prepare(capture,bad,assets);});
    bad=recipe;bad["resources"][0]["replace"]["slicing"]={{"x",8},{"y",0},{"width",1},{"height",1}};rejected([&]{Engine::prepare(capture,bad,assets);});
    std::atomic<unsigned> success{};const auto race=root/(core+"-race");
    auto publish=[&]{try {Engine::publish(prepared,capture.binding,race);++success;}catch(const std::exception&) {}};
    {std::jthread a(publish),b(publish);}assert(success==1);
    const auto quoted=Json(recipe.dump()).dump();Script::Limits limits;
    if(const char* timeout=std::getenv("TIME_SCRIPT_TEST_TIMEOUT_MS"))limits.timeout=std::chrono::milliseconds(std::stoul(timeout));
    for(auto language:{Script::Language::Lua,Script::Language::Python,Script::Language::JavaScript}) {
        if(!Script::ScriptEngine::available(language))continue;
        const auto script="time.report("+quoted+")";
        auto authored=Engine::script(capture,language,script,assets,limits);assert(authored.manifest()==prepared.manifest());
        rejected([&]{Engine::script(capture,language,"time.write8(49152,42)",assets,limits);});
        rejected([&]{Engine::script(capture,language,"time.report('invalid JSON')",assets,limits);});
    }
}
}
int main() {
    auto name=(std::filesystem::temp_directory_path()/"time-authoring-test-XXXXXX").string();assert(mkdtemp(name.data()));
    const std::filesystem::path root=name;
    try {verify("gameboy",root);verify("gamegear",root);}catch(...) {std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);std::cout<<"Both-core authored packs, reviewed bindings, owned assets, atomic publication and scripts passed\n";
}
