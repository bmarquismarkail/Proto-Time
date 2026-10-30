#pragma once
#include "inst_cycle/IntermediateRepresentation.hpp"
namespace BMMQ::IR::Research {
// Opt-in research lowerings. Ordinary core adapters keep this envelope disabled.
// Bus accesses stay byte-wide and ordered; device retirement remains core-owned.
template<class Instruction>
bool lowerExtended(BlockBuilder& b, const Instruction& i, bool gb) {
  using T=ValueType; using O=Opcode;
  const auto op=i.bytes[0];
  const auto next=std::uint16_t(i.address+i.length);
  auto begin=[&](unsigned cycles,bool flow=false){
    b.beginInstruction(i.address,i.length,cycles,flow);
    if(!gb)b.emit(O::CallHelper,{Operand::helper(0)});
  };
  auto reg=[&](unsigned id,T type=T::I16){return b.emitValue(O::ReadRegister,type,{Operand::guestRegister(id,type)});};
  auto constant=[&](std::uint64_t v,T type=T::I16){return b.emitValue(O::Constant,type,{Operand::immediate(v,type)});};
  auto write=[&](unsigned id,ValueId v,T type=T::I16){b.emit(O::WriteRegister,{Operand::guestRegister(id,type),Operand::value(v,type)});};
  auto add=[&](ValueId v,unsigned n,bool sub=false){return b.emitValue(sub?O::Subtract:O::Add,T::I16,{Operand::value(v,T::I16),Operand::value(constant(n),T::I16)});};
  auto load=[&](ValueId a){return b.emitValue(O::LoadMemory,T::I8,{Operand::value(a,T::I16)},MemoryClass::Generic);};
  auto store=[&](ValueId a,ValueId v){b.emit(O::StoreMemory,{Operand::value(a,T::I16),Operand::value(v,T::I8)},MemoryClass::Generic);};
  auto finish=[&](){b.emit(O::SetProgramCounter,{Operand::immediate(next,T::I16)});b.endInstruction();return true;};
  if((op&0xcf)==0x01 && i.length==3){
    begin(gb?12:10);write(9+((op>>4)&3),constant(i.bytes[1]|(i.bytes[2]<<8)));return finish();
  }
  if((op&0xcf)==0x03 || (op&0xcf)==0x0b){
    begin(gb?8:6);auto id=9+((op>>4)&3);write(id,add(reg(id),1,(op&0xcf)==0x0b));return finish();
  }
  if((op==0x20 || op==0x28 || op==0x30 || op==0x38) && i.length==2){
    b.beginInstruction(i.address,i.length,gb?8:7,12,std::nullopt,true);
    if(!gb)b.emit(O::CallHelper,{Operand::helper(0)});
    const unsigned mask=op<0x30?(gb?0x80:0x40):(gb?0x10:1);
    auto f=reg(1,T::I8);auto bits=b.emitValue(O::BitAnd,T::I8,{Operand::value(f,T::I8),Operand::immediate(mask,T::I8)});
    auto take=b.emitValue((op==0x20||op==0x30)?O::CompareEqual:O::CompareNotEqual,T::Bool,{Operand::value(bits,T::I8),Operand::immediate(0,T::I8)});
    b.emit(O::SetProgramCounter,{Operand::immediate(next,T::I16)});
    b.emit(O::BranchIf,{Operand::value(take,T::Bool),Operand::blockTarget(std::uint16_t(next+std::int8_t(i.bytes[1])))});
    b.setTakenCondition(take);b.endInstruction();return true;
  }
  if(op==0xc3 && i.length==3){
    begin(gb?16:10,true);b.emit(O::Branch,{Operand::blockTarget(i.bytes[1]|(i.bytes[2]<<8))});b.endInstruction();return true;
  }
  if(((gb&&(op==0xea||op==0xfa))||(!gb&&(op==0x32||op==0x3a)))&&i.length==3){
    begin(gb?16:13);auto a=constant(i.bytes[1]|(i.bytes[2]<<8));
    if(op==(gb?0xea:0x32))store(a,reg(0,T::I8));else write(0,load(a),T::I8);
    return finish();
  }
  if(gb&&(op==0xe0||op==0xf0||op==0xe2||op==0xf2)){
    if((op==0xe0||op==0xf0)&&i.length!=2)return false;
    begin((op==0xe0||op==0xf0)?12:8);
    auto a=(op==0xe0||op==0xf0)?constant(0xff00+i.bytes[1]):b.emitValue(O::Add,T::I16,{Operand::value(b.emitValue(O::BitAnd,T::I16,{Operand::value(reg(9),T::I16),Operand::immediate(255,T::I16)}),T::I16),Operand::immediate(0xff00,T::I16)});
    if(op==0xe0||op==0xe2)store(a,reg(0,T::I8));else write(0,load(a),T::I8);return finish();
  }
  const bool push=(op&0xcf)==0xc5, pop=(op&0xcf)==0xc1;
  if(push||pop||op==0xcd){
    if(op==0xcd&&i.length!=3)return false;
    begin(op==0xcd?(gb?24:17):push?(gb?16:11):(gb?12:10),op==0xcd);
    auto sp=reg(12);
    auto pair=((op>>4)&3);
    auto highId=pair==3?0:2+2*pair;
    if(push||op==0xcd){
      auto high=op==0xcd?constant(next>>8,T::I8):reg(highId,T::I8);
      auto low=op==0xcd?constant(next&255,T::I8):reg(highId+1,T::I8);
      store(add(sp,1,true),high);store(add(sp,2,true),low);write(12,add(sp,2,true));
      if(op==0xcd){b.emit(O::Branch,{Operand::blockTarget(i.bytes[1]|(i.bytes[2]<<8))});b.endInstruction();return true;}
    }else{
      auto lo=load(sp),hi=load(add(sp,1));
      write(highId+1,lo,T::I8);write(highId,hi,T::I8);write(12,add(sp,2));
    }
    return finish();
  }
  return false;
}
inline bool extendedControl(std::uint8_t op){return op==0xc3||op==0xcd||op==0x20||op==0x28||op==0x30||op==0x38;}
}
