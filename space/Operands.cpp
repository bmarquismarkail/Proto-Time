#include "Capture.hpp"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>
namespace BMMQ::Space {
std::string digest(std::span<const uint8_t> bytes) {
    std::array<unsigned char,SHA256_DIGEST_LENGTH> result{};
    SHA256(bytes.data(),bytes.size(),result.data());
    std::ostringstream out;
    for(auto b:result) out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
    return out.str();
}
OperandInfo decode(std::span<const uint8_t> b,uint16_t pc,uint16_t after,uint8_t flags) {
    (void)after;
    OperandInfo d;
    if(b.empty()) { d.flow="boundary";d.text="interrupt / stall";return d; }
    constexpr uint16_t A=1,F=2,HL=192,SP=256,PC=512;
    constexpr uint16_t r[]={4,8,16,32,64,128,HL,A};
    constexpr const char* names[]={"B","C","D","E","H","L","[HL]","A"};
    constexpr uint16_t pairs[]={12,48,HL,SP};
    constexpr const char* pairNames[]={"BC","DE","HL","SP"};
    auto op=b[0];auto dst=(op>>3)&7,src=op&7;
    auto imm=[&](){return b.size()>2?uint16_t(b[1]|(b[2]<<8)):uint16_t(b.size()>1?b[1]:0);};
    auto hex=[](uint16_t x){std::ostringstream s;s<<"$"<<std::hex<<std::setw(4)<<std::setfill('0')<<x;return s.str();};
    d.reads=PC;d.writes=PC;
    auto read=[&](uint16_t mask){d.reads|=mask;};auto write=[&](uint16_t mask){d.writes|=mask;};
    if(op==0xCB && b.size()==2) {
        auto cb=b[1];src=cb&7; read(r[src]);
        if((cb>>6)!=1 && src!=6)write(r[src]);
        if((cb>>6)<2) { write(F); if((cb>>3 &7)==2 || (cb>>3 &7)==3 || (cb>>6)==1) read(F); }
        const char* rotates[]={"RLC","RRC","RL","RR","SLA","SRA","SWAP","SRL"};
        d.text=(cb<64?std::string(rotates[(cb>>3)&7]):std::string(cb<128?"BIT":cb<192?"RES":"SET")+" "+std::to_string((cb>>3)&7)+",")+" "+names[src];
    } else if(op>=0x40 && op<0x80 && op!=0x76) {
        read(r[src]);if(dst==6)read(HL);else write(r[dst]);
        d.text=std::string("LD ")+names[dst]+", "+names[src];
    } else if(op>=0x80 && op<0xC0) {
        read(A|r[src]);write(F); if((op>>3 &7)!=7)write(A);
        if((op>>3 &7)==1 || (op>>3 &7)==3)read(F);
        const char* alu[]={"ADD","ADC","SUB","SBC","AND","XOR","OR","CP"};
        d.text=std::string(alu[(op>>3)&7])+" A, "+names[src];
    } else if(op<0x40 && (op&7)==6) {
        if(dst==6)read(HL);else write(r[dst]);d.text=std::string("LD ")+names[dst]+", "+hex(imm());
    } else if(op<0x40 && ((op&7)==4 || (op&7)==5)) {
        read(r[dst]|F);write(F);if(dst!=6)write(r[dst]);d.text=std::string((op&1)?"DEC ":"INC ")+names[dst];
    } else if(op<0x40 && (op&15)==1) {
        write(pairs[op>>4]);d.text=std::string("LD ")+pairNames[op>>4]+", "+hex(imm());
    } else if(op<0x40 && ((op&15)==3 || (op&15)==11)) {
        read(pairs[op>>4]);write(pairs[op>>4]);d.text=std::string((op&8)?"DEC ":"INC ")+pairNames[op>>4];
    } else if(op<0x40 && (op&15)==9) {
        read(HL|pairs[op>>4]|F);write(HL|F);d.text=std::string("ADD HL, ")+pairNames[op>>4];
    } else {
        d.text="OP "+hex(op);
        switch(op) {
        case 0x00:d.text="NOP";break;
        case 0x10:d.text="STOP";d.flow="stop";break;
        case 0x76:d.text="HALT";d.flow="halt";break;
        case 0x02:case 0x12:read(A|pairs[op>>4]);d.text="LD ["+std::string(pairNames[op>>4])+"], A";break;
        case 0x0A:case 0x1A:read(pairs[op>>4]);write(A);d.text="LD A, ["+std::string(pairNames[op>>4])+"]";break;
        case 0x22:case 0x32:read(A|HL);write(HL);d.text=op==0x22?"LD [HL+], A":"LD [HL-], A";break;
        case 0x2A:case 0x3A:read(HL);write(A|HL);d.text=op==0x2A?"LD A, [HL+]":"LD A, [HL-]";break;
        case 0x07:case 0x0F:case 0x17:case 0x1F:read(A);if(op>=0x17)read(F);write(A|F);break;
        case 0x27:read(A|F);write(A|F);d.text="DAA";break;
        case 0x2F:read(A|F);write(A|F);d.text="CPL";break;
        case 0x37:case 0x3F:read(F);write(F);d.text=op==0x37?"SCF":"CCF";break;
        case 0x08:read(SP);d.text="LD ["+hex(imm())+"], SP";break;
        case 0xE0:case 0xEA:read(A);d.text="LD ["+hex(op==0xE0?uint16_t(0xFF00|imm()):imm())+"], A";break;
        case 0xE2:read(A|8);d.text="LD [C], A";break;
        case 0xF0:case 0xFA:write(A);d.text="LD A, ["+hex(op==0xF0?uint16_t(0xFF00|imm()):imm())+"]";break;
        case 0xF2:read(8);write(A);d.text="LD A, [C]";break;
        case 0xE8:read(SP);write(SP|F);d.text="ADD SP, offset";break;
        case 0xF8:read(SP);write(HL|F);d.text="LD HL, SP+offset";break;
        case 0xF9:read(HL);write(SP);d.text="LD SP, HL";break;
        case 0xF3:d.text="DI";break;case 0xFB:d.text="EI";break;
        default:break;
        }
        if((op&0xC7)==0xC6) {read(A);write(F);if((op>>3&7)!=7)write(A);if((op>>3&7)==1 || (op>>3&7)==3)read(F);}
        if((op&0xCF)==0xC1) { read(SP);write(SP|(op==0xF1?uint16_t(A|F):pairs[(op>>4)&3]));d.text="POP"; }
        if((op&0xCF)==0xC5) { read(SP|(op==0xF5?uint16_t(A|F):pairs[(op>>4)&3]));write(SP);d.text="PUSH"; }
        bool jr=op==0x18 || op==0x20 || op==0x28 || op==0x30 || op==0x38;
        bool jp=op==0xC3 || op==0xC2 || op==0xCA || op==0xD2 || op==0xDA;
        bool call=op==0xCD || op==0xC4 || op==0xCC || op==0xD4 || op==0xDC;
        bool ret=op==0xC9 || op==0xD9 || op==0xC0 || op==0xC8 || op==0xD0 || op==0xD8;
        bool rst=(op&0xC7)==0xC7;
        if(jr||jp||call||ret||rst||op==0xE9) {
            d.flow=call||rst?"call":ret?"return":op==0xE9?"indirect":"branch";
            d.conditional=(jr && op!=0x18)||(jp && op!=0xC3)||(call && op!=0xCD)||(ret && op!=0xC9 && op!=0xD9);
            if(d.conditional)read(F);
            d.direct=jr||jp||call||rst;
            d.target=jr?uint16_t(pc+b.size()+int8_t(imm())):rst?uint16_t(op&0x38):imm();
            if(op==0xE9)read(HL);
            const auto condition=(op>>3)&3;
            const bool conditionSet=(flags&(condition<2?0x80:0x10))!=0;
            bool taken=!d.conditional||(conditionSet==bool(condition&1));
            d.taken=taken;
            if((call||ret||rst)&&(!d.conditional||taken)){read(SP);write(SP);}
            d.text=(call?"CALL ":ret?"RET ":jr?"JR ":rst?"RST ":"JP ")+ (op==0xE9?std::string("HL"):ret?std::string():hex(d.target));
            if(d.conditional)d.text+=" (conditional)";
        }
    }
    return d;
}
}
