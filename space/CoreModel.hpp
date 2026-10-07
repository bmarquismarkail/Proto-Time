#pragma once
#include "Capture.hpp"
#include <vector>
#include <stdexcept>
namespace BMMQ::Space {
struct RegisterLane {const char* name;unsigned pair;bool high=false;bool byte=false;};
struct CoreModel {
    std::string_view id;
    std::span<const char* const> pairs;
    std::span<const RegisterLane> lanes;
    uint32_t maximumInstructionLength;
    uint16_t registerMaximum(size_t pair)const noexcept {
        if(id!="gamegear"||pair<12)return 65535;
        if(pair==12||pair==13)return 255;
        return pair==17||pair==19?2:1;
    }
    uint16_t laneMaximum(size_t lane)const noexcept {auto d=lanes[lane];return d.byte?std::min<uint16_t>(255,registerMaximum(d.pair)):registerMaximum(d.pair);}
    uint16_t lane(const Record& record,size_t n)const {
        auto d=lanes[n];auto value=record.registers[d.pair];
        return d.byte?uint16_t(d.high?value>>8:value&255):value;
    }
};
const CoreModel& coreModel(std::string_view core);
OperandInfo decodeCore(std::string_view core,std::span<const uint8_t> bytes,uint16_t pc,uint16_t after,uint8_t flags=0);
// Returns the required encoded length; zero means a missing prefix/opcode byte.
size_t z80InstructionLength(std::span<const uint8_t>);
struct DirectTarget {bool direct=false;uint16_t address=0;};
DirectTarget z80DirectTarget(std::span<const uint8_t>,uint16_t pc)noexcept;
}
