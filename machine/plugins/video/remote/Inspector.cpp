#include "Inspector.hpp"
#include "cores/gameboy/video/GameBoyVisualDebugAdapter.hpp"
#include "cores/gamegear/GameGearVisualDebugAdapter.hpp"
#include "machine/plugins/video/RealtimeVideoSurface.hpp"
#include <stdexcept>
namespace BMMQ::RemoteVideo {
using Json=nlohmann::json;
bool Mailbox::publish(Snapshot&& value) noexcept {
    if(value.frame.width!=160 || value.frame.height!=144 || value.frame.empty() || value.video.vram.size()>16384 || value.video.deviceState.size()>65536 ||
       value.video.oam.size()>160 || value.video.deviceRegisters.size()>256) return false;
    slots_[producer_]=std::move(value);
    const auto previous=shared_.exchange(std::uint8_t(128|producer_),std::memory_order_acq_rel);
    producer_=previous&3;
    if(previous&128) drops_.fetch_add(1,std::memory_order_relaxed);
    return true;
}
std::optional<Snapshot> Mailbox::consume() noexcept {
    if(!(shared_.load(std::memory_order_acquire)&128))return {};
    const auto previous=shared_.exchange(consumer_,std::memory_order_acq_rel);
    if(!(previous&128))return {};
    consumer_=previous&3;
    return std::move(slots_[consumer_]);
}
Inspector::Inspector(std::string core):core_(std::move(core)) {
    if(core_!="gameboy" && core_!="gamegear") throw std::invalid_argument("unsupported video core");
}
namespace {
std::uint64_t integer(const Json& j,const char* key,std::uint64_t max) {
    const auto& v=j.at(key);
    if(!v.is_number_unsigned() && !(v.is_number_integer() && v.get<std::int64_t>()>=0))throw std::invalid_argument("unsigned integer required");
    const auto n=v.get<std::uint64_t>();if(n>max)throw std::invalid_argument("integer exceeds contract");return n;
}
Json pixels(const std::vector<std::uint32_t>& argb) {
    // Compact hex RGBA avoids JSON numeric-array expansion and is canvas-ready.
    constexpr char hex[]="0123456789abcdef";std::string text; text.reserve(argb.size()*8);
    for(auto color:argb)for(auto shift:{16,8,0,24}) {auto byte=(color>>shift)&255;text+=hex[byte>>4];text+=hex[byte&15];}
    return text;
}
}
Json Inspector::reply(const Debug::Reply& r) {
    Json j{{"id",r.id},{"error",unsigned(r.error)},{"state",unsigned(r.state)},{"reason",unsigned(r.reason)},
        {"generation",std::to_string(r.generation)},{"pauseId",std::to_string(r.pauseId)},
        {"traceLost",r.lost},{"requestOverflow",r.requestOverflow},{"rejectedActions",r.rejectedActions},{"registers",r.registers}};
    j["memory"]=Json::array();for(unsigned i=0;i<r.length && i<r.memory.size();++i)j["memory"].push_back(r.memory[i]);return j;
}
Debug::Command Inspector::command(const Json& j,std::uint32_t id) {
    if(!j.is_object())throw std::invalid_argument("command object required");
    Debug::Command c; c.id=id;
    // Exact decimal strings retain 64-bit identity in browser JavaScript.
    auto identity=[&](const char* key) {const auto& s=j.at(key).get_ref<const std::string&>();
        if(s.empty() || s.size()>20 || s.find_first_not_of("0123456789")!=s.npos)throw std::invalid_argument("invalid identity");
        std::size_t used;auto n=std::stoull(s,&used);if(used!=s.size())throw std::invalid_argument("invalid identity");return n;};
    c.generation=identity("generation");c.pauseId=identity("pauseId");
    const auto op=j.at("operation").get<std::string>();
    if(op=="pause")c.operation=Debug::Operation::Pause;
    else if(op=="continue")c.operation=Debug::Operation::Continue;
    else if(op=="step") {c.operation=Debug::Operation::Step;c.steps=integer(j,"steps",10'000'000);if(!c.steps)throw std::invalid_argument("positive steps required");}
    else if(op=="inspect") {c.operation=Debug::Operation::Inspect;c.address=integer(j,"address",65535);c.length=integer(j,"length",256);
        if(unsigned(c.address)+c.length>65536)throw std::invalid_argument("memory range overflow");}
    else if(op=="edit") {c.operation=Debug::Operation::Edit;const auto& bytes=j.at("bytes");if(!bytes.is_array() || bytes.size()>64)throw std::invalid_argument("edit budget");
        for(const auto& byte:bytes)c.bytes[c.byteCount++]={std::uint16_t(integer(byte,"address",65535)),std::uint8_t(integer(byte,"value",255))};
        if(j.contains("registers")) {const auto& regs=j.at("registers");if(!regs.is_array() || regs.size()!=20)throw std::invalid_argument("register extent");
            c.editRegisters=true;for(unsigned i=0;i<20;++i) {Json one{{"value",regs[i]}};c.registers[i]=integer(one,"value",65535);}}
    } else if(op=="rules") {c.operation=Debug::Operation::Rules;const auto& breaks=j.at("breakpoints");const auto& watches=j.at("watchpoints");
        if(!breaks.is_array() || !watches.is_array() || breaks.size()>64 || watches.size()>64)throw std::invalid_argument("rule budget");
        for(const auto& bp:breaks)c.breaks[c.breakCount++]={std::uint16_t(integer(bp,"address",65535)),0,false};
        for(const auto& w:watches)c.watches[c.watchCount++]={std::uint16_t(integer(w,"first",65535)),std::uint16_t(integer(w,"last",65535)),Debug::Access(integer(w,"access",3))};
    } else throw std::invalid_argument("unsupported browser operation");
    return c;
}
Json Inspector::decode(const Snapshot& s,std::uint64_t drops) const {
    const bool gg=core_=="gamegear";
    if(s.video.machineId!=core_ || s.video.vram.size()!=(gg?16384u:8192u) || s.video.oam.size()!=160 ||
       s.frame.contractVersion!=RealtimeVideoPacket::kContractVersion || s.frame.width!=160 || s.frame.height!=144 || s.frame.empty())
        throw std::invalid_argument("invalid owned video snapshot");
    std::vector<std::uint32_t> frame(23040);
    if(!decodeVideoSurfaceToArgb(s.frame.surface,160,144,frame.data(),160)) throw std::invalid_argument("invalid frame encoding");
    const GB::GameBoyVisualDebugAdapter gbAdapter; const GameGearVisualDebugAdapter ggAdapter;
    const IVisualDebugAdapter& adapter=gg?static_cast<const IVisualDebugAdapter&>(ggAdapter):static_cast<const IVisualDebugAdapter&>(gbAdapter);
    const auto model=adapter.buildFrameModelFromState(s.video,{160,144});
    if(!model || model->empty() || model->semantics.size()!=23040 || model->resources.size()>23040)throw std::invalid_argument("video semantic model unavailable");
    std::vector<std::uint32_t> palette;
    if(gg) {GameGearVDP vdp;vdp.importState(s.video.deviceState);const auto& cram=vdp.debugCram();
        for(unsigned i=0;i<32;++i)palette.push_back(0xff000000u|((cram[2*i]&15)*17<<16)|((cram[2*i]>>4)*17<<8)|((cram[2*i+1]&15)*17));
    } else palette={0xffe0f8d0,0xff88c070,0xff346856,0xff081820};
    const unsigned count=gg?512:384, width=128,height=count/16*8;
    std::vector<std::uint32_t> tiles(width*height),spriteTiles(width*height);
    for(unsigned i=0;i<count;++i) {
        VisualTileDecodeRequest request;request.tileIndex=i;request.tileAddress=gg?i*32:0x8000+i*16;request.paletteValue=s.video.bgp;request.paletteRegister="BGP";
        const auto tile=adapter.decodeTile(s.video.vram,s.video.bgp,s.video.obp0,s.video.obp1,request);
        if(!tile || tile->pixels.size()!=64)throw std::invalid_argument("tile decoding unavailable");
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            auto color=tile->pixels[y*8+x];
            if(!gg) color=(s.video.bgp>>(color*2))&3;
            if(color>=palette.size())throw std::invalid_argument("tile palette out of bounds");
            const auto at=(i/16*8+y)*width+i%16*8+x;
            tiles[at]=palette[color];
            spriteTiles[at]=gg?palette[color+16]:palette[color];
        }
    }
    Json sprites=s.video.oam;
    std::uint16_t spriteBase=0;
    if(gg) {if(s.video.deviceRegisters.size()!=11)throw std::invalid_argument("VDP register extent");
        spriteBase=(s.video.deviceRegisters[5]&0x7e)<<7;
        sprites=Json::array();for(unsigned i=0;i<64;++i)sprites.push_back({s.video.vram[(spriteBase+i)&0x3fff],
            s.video.vram[(spriteBase+128+i*2)&0x3fff],s.video.vram[(spriteBase+129+i*2)&0x3fff]});
    }
    Json resources=Json::array(),semantics=Json::array();
    for(const auto& resource:model->resources) {const auto& d=resource.descriptor;
        resources.push_back({{"kind",visualResourceKindName(d.kind)},{"index",d.source.index},{"address",d.source.address},
                            {"palette",d.source.paletteValue},{"label",d.source.label},{"sourceHash",toHexVisualHash(d.sourceHash)}});}
    for(const auto& p:model->semantics)semantics.push_back(p.hasResource()?Json(p.resourceIndex):Json(nullptr));
    return {{"schemaVersion",1},{"core",core_},{"sequence",std::to_string(s.sequence)},{"cycles",std::to_string(s.cycles)},
        {"droppedSnapshots",drops},{"cpu",reply(s.cpu)},{"frame",{{"width",160},{"height",144},{"rgba",pixels(frame)},
        {"displayEnabled",s.frame.displayEnabled},{"inVBlank",s.frame.inVBlank}}},
        {"atlas",{{"width",width},{"height",height},{"tileCount",count},{"rgba",pixels(tiles)},{"rgbaSprite",pixels(spriteTiles)}}},
        {"paletteRgba",pixels(palette)},{"paletteRegisters",{s.video.bgp,s.video.obp0,s.video.obp1}},
        {"sprites",sprites},{"spriteTableAddress",spriteBase},{"vram",s.video.vram},{"oam",s.video.oam},{"videoRegisters",s.video.deviceRegisters},
        {"resources",resources},{"semantics",semantics}};
}
}
