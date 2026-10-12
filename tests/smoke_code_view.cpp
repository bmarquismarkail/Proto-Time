#include "cores/gameboy/GameBoyMemoryMap.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearCartridge.hpp"
#include "WholeBlockFixtures.hpp"
#include "inst_cycle/research/MachineBlock.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace BMMQ::IR;
using namespace BMMQ::IR::Research;
void require(bool b) { if (!b) throw std::runtime_error("checked code view contract"); }
template<class Map, class Peek> void parity(Map& map, Peek peek) {
    for (unsigned a = 0; a < 65536; ++a) {
        for (std::size_t n : {1u, 2u, 16u, 257u}) {
            auto view = map.checkedCodeView(a,n);
            if (view.empty()) continue;
            require(view.size() == n && n <= 65536-a);
            for (std::size_t i=0;i<n;++i) { uint8_t b=0; require(peek(a+i,b) && b==view[i]); }
        }
    }
    require(map.checkedCodeView(0,0).empty());
    require(map.checkedCodeView(0xffff,2).empty());
    require(map.checkedCodeView(1,~std::size_t{0}).empty());
}
struct CustomCartridge : GameGearCartridge { uint8_t read(uint16_t) const override { return 0x42; } };
int main() try {
    std::vector<uint8_t> rom(0x10003);
    for (std::size_t i=0;i<rom.size();++i) rom[i]=(i*17+i/0x4000)&255;
    GB::GameBoyMemoryMap gb;
    gb.installRomWindow(0,{rom.data(),0x4000});
    gb.installRomWindow(0x4000,{rom.data()+0x4000,0x4000});
    gb.mapBootRom(rom.data()+0x8000,256);
    auto gbPeek=[&](uint16_t a,uint8_t& b){return gb.peekExecutableByte(a,b);};
    parity(gb,gbPeek);
    require(gb.checkedCodeView(255,2).empty()); require(gb.checkedCodeView(0x3fff,2).empty());
    gb.write(0xff50,1); parity(gb,gbPeek);
    gb.installRomWindow(0x4000,{rom.data(),3});
    require(gb.checkedCodeView(0x4002,2).empty()); parity(gb,gbPeek);
    gb.clearRomWindow(0); require(gb.checkedCodeView(0,1).empty());
    gb.reset(); parity(gb,gbPeek);
    GameGearCartridge cart; require(cart.load(rom.data(),rom.size()));
    GameGearMemoryMap gg; gg.setCartridge(&cart);
    auto ggPeek=[&](uint16_t a,uint8_t& b){return gg.peekCodeByte(a,b);};
    parity(gg,ggPeek);
    for (uint16_t a : {0xdb,0xdc,0xdd,0x3ff,0x3fff,0x7fff,0xbfff,0xdfff,0xfdff}) require(gg.checkedCodeView(a,2).empty());
    for (uint8_t bank : {0,1,4,255}) { gg.write(0xfffe,bank); parity(gg,ggPeek); }
    gg.write(0xfffc,8); require(gg.checkedCodeView(0x8000,1).empty()); parity(gg,ggPeek);
    std::vector<uint8_t> bios(19,0x55); gg.mapBios(bios.data(),bios.size()); gg.writeIoPort(0x3e,0);
    require(gg.checkedCodeView(18,2).empty()); parity(gg,ggPeek);
    gg.clearBios(); parity(gg,ggPeek);
    CustomCartridge custom; require(custom.load(rom.data(),rom.size())); gg.setCartridge(&custom);
    require(gg.checkedCodeView(0x400,1).empty()); uint8_t byte=0; require(ggPeek(0x400,byte)&&byte==0x42);
    // Exact-byte comparison, fallback, scalar rejection and scalar-only checks.
    auto artifact=emittedGameBoyBank(); auto owner=std::make_shared<OwnerIdentity>();
    GB::IRExecution::GameBoyCoreAdapter adapter;
    auto bound=bindMachineBlock(artifact,adapter,owner,1,77);
    for (const auto& guard:artifact.block().guards) if(guard.kind==GuardKind::CodeBytes) {
        auto bytes=guard.bytes; std::size_t peeks=0,views=0;
        auto peek=[&](uint16_t a,uint8_t& b){++peeks;if(a<guard.subject||a-guard.subject>=bytes.size())return false;b=bytes[a-guard.subject];return true;};
        auto view=[&](uint16_t a,std::size_t n){++views;require(a==guard.subject&&n==bytes.size());return std::span<const uint8_t>(bytes);};
        require(bound.guardsMatchView(77,0,1,peek,view)&&peeks==0&&views==1);
        for(std::size_t i=0;i<bytes.size();++i){bytes[i]^=1;require(!bound.guardsMatchView(77,0,1,peek,view));bytes[i]^=1;}
        auto absent=[](uint16_t,std::size_t){return std::span<const uint8_t>{};};
        require(bound.guardsMatchView(77,0,1,peek,absent)&&peeks==bytes.size());
        peeks=views=0;require(!bound.guardsMatchView(78,0,1,peek,view)&&peeks==0&&views==0);
        require(bound.guardsMatchView(77,0,1,peek,view,false)&&peeks==0&&views==0);
    }
    std::cout<<"checked view parity, boundaries, mapper semantics and exact guards passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
