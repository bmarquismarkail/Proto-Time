#include "CoreModel.hpp"
#include <sstream>
#include <iomanip>
namespace BMMQ::Space {
namespace {
constexpr const char* gbPairs[]={"AF","BC","DE","HL","SP","PC"};
constexpr RegisterLane gbLanes[]={{"A",0,true,true},{"F",0,false,true},{"B",1,true,true},{"C",1,false,true},
    {"D",2,true,true},{"E",2,false,true},{"H",3,true,true},{"L",3,false,true},{"SP",4},{"PC",5}};
constexpr const char* ggPairs[]={"AF","BC","DE","HL","SP","PC","IX","IY","AF'","BC'","DE'","HL'",
    "I","R","IFF1","IFF2","IME","IM","HALT","EI_DELAY"};
constexpr RegisterLane ggLanes[]={
    {"A",0,true,true},{"F",0,false,true},{"B",1,true,true},{"C",1,false,true},
    {"D",2,true,true},{"E",2,false,true},{"H",3,true,true},{"L",3,false,true},{"SP",4},{"PC",5},
    {"IX",6},{"IY",7},{"A'",8,true,true},{"F'",8,false,true},{"B'",9,true,true},{"C'",9,false,true},
    {"D'",10,true,true},{"E'",10,false,true},{"H'",11,true,true},{"L'",11,false,true},
    {"I",12,false,true},{"R",13,false,true},{"IFF1",14,false,true},{"IFF2",15,false,true},
    {"IME",16,false,true},{"IM",17,false,true},{"HALT",18,false,true},{"EI_DELAY",19,false,true}};
const CoreModel gb{"gameboy",gbPairs,gbLanes,3},gg{"gamegear",ggPairs,ggLanes,65539};
std::string hex(unsigned v){std::ostringstream s;s<<"$"<<std::hex<<std::setw(2)<<std::setfill('0')<<v;return s.str();}
}
const CoreModel& coreModel(std::string_view id){if(id==gb.id)return gb;if(id==gg.id)return gg;throw std::invalid_argument("unsupported S.P.A.C.E. core");}
size_t z80InstructionLength(std::span<const uint8_t> bytes){
    size_t prefixes=0;while(prefixes<bytes.size()&&(bytes[prefixes]==0xdd||bytes[prefixes]==0xfd)){
        if(++prefixes==65536)return prefixes;
    }
    if(prefixes==bytes.size())return 0;
    bool indexed=prefixes!=0;auto op=bytes[prefixes];unsigned suffix=1;
    if(op==0xcb)suffix=indexed?3:2;
    else if(op==0xed){if(prefixes+1>=bytes.size())return 0;suffix=(bytes[prefixes+1]&0xc7)==0x43?4:2;}
    else if(op<0x40){
        if((op&7)==0&&op>=0x10)suffix=2;
        else if((op&15)==1)suffix=3;
        else if(((op&15)==2||(op&15)==10)&&op>=0x20)suffix=3;
        else if((op&7)==6)suffix=2+unsigned(indexed&&((op>>3)&7)==6);
        else if(indexed&&((op>>3)&7)==6&&((op&7)==4||(op&7)==5))suffix=2;
    }else if(op<0xc0){if(indexed&&op!=0x76&&((op&7)==6||(op<0x80&&((op>>3)&7)==6)))suffix=2;}
    else if((op&0xc7)==0xc2||(op&0xc7)==0xc4||op==0xc3||op==0xcd)suffix=3;
    else if((op&0xc7)==0xc6||op==0xd3||op==0xdb)suffix=2;
    return prefixes+suffix;
}
DirectTarget z80DirectTarget(std::span<const uint8_t> bytes,uint16_t pc)noexcept{
    size_t i=0;while(i<bytes.size()&&(bytes[i]==0xdd||bytes[i]==0xfd))++i;
    if(i==bytes.size())return {};auto op=bytes[i++];
    if(op==0x10||op==0x18||op==0x20||op==0x28||op==0x30||op==0x38){
        if(i>=bytes.size())return {};return {true,uint16_t(pc+bytes.size()+int8_t(bytes[i]))};
    }
    if(op==0xc3||op==0xcd||(op&0xc7)==0xc2||(op&0xc7)==0xc4){
        if(i+1>=bytes.size())return {};return {true,uint16_t(bytes[i]|(uint16_t(bytes[i+1])<<8))};
    }
    if((op&0xc7)==0xc7)return {true,uint16_t(op&0x38)};
    if(op==0xed&&i<bytes.size()&&(bytes[i]&0xe4)==0xa0&&(bytes[i]&0x10))return {true,pc};
    return {};
}
OperandInfo decodeCore(std::string_view core,std::span<const uint8_t> bytes,uint16_t pc,uint16_t after,uint8_t flags){
    if(core=="gameboy")return decode(bytes,pc,after,flags);
    (void)coreModel(core);
    OperandInfo d;d.reads=1u<<9;d.writes=(1u<<9)|(1u<<21); // PC and refresh
    if(bytes.empty()){d.flow="boundary";d.text="interrupt / stall";return d;}
    size_t i=0;unsigned index=0;
    while(i<bytes.size()&&(bytes[i]==0xdd||bytes[i]==0xfd)){index=bytes[i++]==0xdd?10:11;}
    if(i==bytes.size()){d.text="index-prefix wrap";return d;}
    const auto op=bytes[i++];const uint32_t A=1,F=2,BC=12,DE=48,HL=index?(1u<<index):192,SP=256;
    const uint32_t r[]={4,8,16,32,index?HL:64,index?HL:128,HL,A};
    const char* names[]={"B","C","D","E",index?(index==10?"IXH":"IYH"):"H",index?(index==10?"IXL":"IYL"):"L",index?(index==10?"[IX+d]":"[IY+d]"):"[HL]","A"};
    const uint32_t pairs[]={BC,DE,HL,SP};
    auto rd=[&](uint32_t m){d.reads|=m;};auto wr=[&](uint32_t m){d.writes|=m;};
    auto immediate=[&](){return i+1<bytes.size()?uint16_t(bytes[i]|(bytes[i+1]<<8)):uint16_t(i<bytes.size()?bytes[i]:0);};
    auto condition=[&](unsigned n){const unsigned masks[]={0x40,0x40,1,1,4,4,0x80,0x80};return bool(flags&masks[n])==bool(n&1);};
    auto flow=[&](std::string f,bool conditional=false,unsigned c=0){d.flow=std::move(f);d.conditional=conditional;d.taken=!conditional||condition(c);if(conditional)rd(F);};
    d.text="Z80 "+hex(op);
    if(op==0xcb){if(index&&i<bytes.size())++i;unsigned cb=i<bytes.size()?bytes[i]:0;unsigned operand=cb&7;
        rd(index?HL:r[operand]);if(cb<128)wr(F);if(cb>=64&&cb<128)rd(F);if(cb<64&&((cb>>3)&7)==2)rd(F);if(cb<64&&((cb>>3)&7)==3)rd(F);
        if((cb>>6)!=1&&operand!=6)wr(index?(operand==4?64:operand==5?128:r[operand]):r[operand]);
        d.text=std::string(cb<64?"rotate ":cb<128?"BIT ":cb<192?"RES ":"SET ")+names[index?6:operand];return d;}
    if(op==0xed){
        // Index prefixes are ignored for ED encodings, including HL operands.
        const uint32_t r[]={4,8,16,32,64,128,192,A};
        const uint32_t pairs[]={BC,DE,192,SP};
        unsigned ed=i<bytes.size()?bytes[i]:0;
        d.text="ED "+hex(ed);rd(1u<<21); // additional opcode refresh
        if((ed&0xc7)==0x40){rd(BC|F);if(((ed>>3)&7)!=6)wr(r[(ed>>3)&7]);wr(F);}
        else if((ed&0xc7)==0x41){rd(BC);if(((ed>>3)&7)!=6)rd(r[(ed>>3)&7]);}
        else if((ed&0xcf)==0x42||(ed&0xcf)==0x4a){rd(192|pairs[(ed>>4)&3]|F);wr(192|F);}
        else if((ed&0xcf)==0x43){rd(pairs[(ed>>4)&3]);}
        else if((ed&0xcf)==0x4b){wr(pairs[(ed>>4)&3]);}
        else if((ed&0xc7)==0x44){rd(A);wr(A|F);}
        else if((ed&0xc7)==0x45){rd(SP|(1u<<23));wr(SP|(1u<<22)|(1u<<24));flow("return");}
        else if((ed&0xc7)==0x46){wr(1u<<25);}
        else if(ed==0x47||ed==0x4f){rd(A);wr(1u<<(ed==0x47?20:21));}
        else if(ed==0x57||ed==0x5f){rd(F|(1u<<23)|(1u<<(ed==0x57?20:21)));wr(A|F);}
        else if(ed==0x67||ed==0x6f){rd(A|192|F);wr(A|F);}
        else if((ed&0xe4)==0xa0){unsigned kind=ed&3;rd(BC|192|F);wr(BC|192|F);
            if(kind==0){rd(DE|A);wr(DE);}else if(kind==1)rd(A);
            if(ed&0x10){flow("branch",true);d.direct=true;d.target=pc;d.taken=after==pc;}}
        return d;
    }
    if(op>=0x40&&op<0x80&&op!=0x76){
        auto source=op&7,destination=(op>>3)&7;
        // LD H/L,(IX+d) and LD (IX+d),H/L retain ordinary H/L.
        auto operand=[&](unsigned n){return index&&(source==6||destination==6)&&n==4?64u:index&&(source==6||destination==6)&&n==5?128u:r[n];};
        rd(operand(source));if(destination==6)rd(HL);else wr(operand(destination));d.text=std::string("LD ")+names[(op>>3)&7]+", "+names[op&7];}
    else if(op>=0x80&&op<0xc0){rd(A|r[op&7]);wr(F);if(((op>>3)&7)!=7)wr(A);if(((op>>3)&7)==1||((op>>3)&7)==3)rd(F);}
    else if(op<0x40&&(op&7)==6){if(((op>>3)&7)==6)rd(HL);else wr(r[(op>>3)&7]);}
    else if(op<0x40&&((op&7)==4||(op&7)==5)){rd(r[(op>>3)&7]|F);wr(F);if(((op>>3)&7)!=6)wr(r[(op>>3)&7]);}
    else if(op<0x40&&(op&15)==1)wr(pairs[op>>4]);
    else if(op<0x40&&((op&15)==3||(op&15)==11)){rd(pairs[op>>4]);wr(pairs[op>>4]);}
    else if(op<0x40&&(op&15)==9){rd(HL|pairs[op>>4]|F);wr(HL|F);}
    else if((op&0xc7)==0xc0){flow("return",true,(op>>3)&7);if(d.taken){rd(SP);wr(SP);}}
    else if((op&0xc7)==0xc2||(op&0xc7)==0xc4){flow((op&7)==4?"call":"branch",true,(op>>3)&7);d.direct=true;d.target=immediate();if(d.flow=="call"&&d.taken){rd(SP);wr(SP);}}
    else if((op&0xcf)==0xc1){rd(SP);wr(SP|((op>>4)==3?A|F:pairs[(op>>4)&3]));}
    else if((op&0xcf)==0xc5){rd(SP|((op>>4)==3?A|F:pairs[(op>>4)&3]));wr(SP);}
    else if((op&0xc7)==0xc6){rd(A);wr(F);if(((op>>3)&7)!=7)wr(A);if(((op>>3)&7)==1||((op>>3)&7)==3)rd(F);}
    else if((op&0xc7)==0xc7){rd(SP);wr(SP);flow("call");d.direct=true;d.target=op&0x38;}
    else switch(op){
        case 0x00:d.text="NOP";break;
        case 0x02:rd(A|BC);break;case 0x12:rd(A|DE);break;case 0x0a:rd(BC);wr(A);break;case 0x1a:rd(DE);wr(A);break;
        case 0x07:case 0x0f:case 0x17:case 0x1f:rd(A|F);wr(A|F);break;
        case 0x08:rd(A|F|(3u<<12));wr(A|F|(3u<<12));break;
        case 0x10:rd(4);wr(4);d.flow="branch";d.conditional=true;d.taken=after!=uint16_t(pc+bytes.size());d.direct=true;d.target=uint16_t(pc+bytes.size()+int8_t(i<bytes.size()?bytes[i]:0));break;
        case 0x18:case 0x20:case 0x28:case 0x30:case 0x38:flow("branch",op!=0x18,op>=0x30?2+(op==0x38):unsigned(op==0x28));d.direct=true;d.target=uint16_t(pc+bytes.size()+int8_t(i<bytes.size()?bytes[i]:0));break;
        case 0x22:rd(HL);break;case 0x2a:wr(HL);break;case 0x32:rd(A);break;case 0x3a:wr(A);break;
        case 0x27:case 0x2f:rd(A|F);wr(A|F);break;case 0x37:case 0x3f:rd(A|F);wr(F);break;
        case 0x76:wr(1u<<26);flow("halt");break;
        case 0xc3:flow("branch");d.direct=true;d.target=immediate();break;
        case 0xc9:rd(SP);wr(SP);flow("return");break;
        case 0xcd:rd(SP);wr(SP);flow("call");d.direct=true;d.target=immediate();break;
        case 0xd3:rd(A);break;case 0xdb:rd(A);wr(A);break;
        case 0xd9:rd(BC|DE|192|(0x3fu<<14));wr(BC|DE|192|(0x3fu<<14));break;
        case 0xe3:rd(SP|HL);wr(HL);break;case 0xe9:rd(HL);flow("indirect");break;
        case 0xeb:rd(DE|192);wr(DE|192);break;
        case 0xf3:case 0xfb:wr((1u<<22)|(1u<<23)|(1u<<24)|(1u<<27));break;
        case 0xf9:rd(HL);wr(SP);break;
    }
    return d;
}
}
