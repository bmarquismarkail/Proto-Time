#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
// Original fixture authored for S.P.A.C.E.; not derived from a commercial ROM.
inline std::vector<uint8_t> spaceFixture(){
    std::vector<uint8_t> rom(0x8000,0);
    rom[0x100]=0xC3;rom[0x101]=0x50;rom[0x102]=1;
    const std::vector<uint8_t> main={0x31,0xf0,0xdf,0x3e,1,0xea,0,0xc0,0xcd,0x70,1,0xfa,0,0xc0,0x3e,3,0xea,0,0xc0,0xc3,0x80,1};
    const std::vector<uint8_t> sub={0x3e,2,0xea,0,0xc0,0xc9};
    const std::vector<uint8_t> loop={0x21,0,0xc0,0x7e,0x77,0x18,0xfc};
    std::copy(main.begin(),main.end(),rom.begin()+0x150);std::copy(sub.begin(),sub.end(),rom.begin()+0x170);std::copy(loop.begin(),loop.end(),rom.begin()+0x180);
    return rom;
}
