#define private public
#include "cores/gamegear/Z80Interpreter.hpp"
#undef private

#include "cores/gamegear/GameGearCartridge.hpp"
#include "cores/gamegear/GameGearInput.hpp"
#include "cores/gamegear/GameGearMapperFactory.hpp"
#include "cores/gamegear/GameGearMemoryMap.hpp"
#include "cores/gamegear/GameGearPSG.hpp"
#include "cores/gamegear/GameGearVDP.hpp"

#include "machine/InputTypes.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct IoEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    char kind = 'R';
    uint8_t port = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
    bool afterC702Nonzero = false;
    bool inInterruptRoutine = false;
};

struct OpcodeWindow {
    uint16_t start = 0;
    std::array<uint8_t, 17> bytes{};
};

struct MemEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t address = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
    OpcodeWindow opcodes{};
};

struct RamWriteEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t address = 0;
    uint8_t previousValue = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
    OpcodeWindow opcodes{};
    uint16_t af = 0;
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t ix = 0;
    uint16_t iy = 0;
    uint16_t sp = 0;
    bool machine_irq_pending = false;
    bool vdp_frame_pending = false;
    bool vdp_line_pending = false;
};

struct C702WriteEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint8_t value = 0;
    uint8_t previousValue = 0;
    uint8_t scanline = 0;
    OpcodeWindow opcodes{};
};

struct C702ReadEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
};

struct InterruptEvent {
    uint64_t step = 0;
    uint16_t pcBefore = 0;
    uint16_t pcAfter = 0;
    uint16_t spAfter = 0;
    uint8_t vector = 0;
    uint8_t mode = 0;
    uint8_t scanline = 0;
    bool afterC702Nonzero = false;
    bool vdpIrqAsserted = false;
    bool framePending = false;
    bool linePending = false;
    bool ie0 = false;
    bool ie1 = false;
};

struct VdpRegisterWriteEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint8_t reg = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
    bool ie0 = false;
    bool ie1 = false;
    uint8_t lineReload = 0;
};

struct BankState {
    bool available = false;
    std::array<uint8_t, 3> registers{};
    uint8_t control = 0;
};

struct CpuSnapshot {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t af = 0;
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t ix = 0;
    uint16_t iy = 0;
    uint16_t sp = 0;
    BankState banks{};
    uint8_t c702Before = 0;
    uint8_t c702After = 0;
    OpcodeWindow opcodes{};
    // Memory snapshots for targeted regions
    std::vector<uint8_t> ram_c700_c720;
    std::vector<uint8_t> ram_c780_c7c0;
    std::vector<uint8_t> ram_c880_c8a0;
    std::vector<uint8_t> ram_c4e0_c720;
    // IX/IY-relative regions (IX-16..IX+32, IY-16..IY+32)
    std::vector<uint8_t> ram_ix_range;
    std::vector<uint8_t> ram_iy_range;
};

struct BranchEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t target = 0;
    uint16_t fallthrough = 0;
    uint8_t opcode = 0;
    uint8_t operand0 = 0;
    uint8_t operand1 = 0;
    bool conditional = false;
    bool taken = false;
    bool inNeighborhoodBefore4B24 = false;
    CpuSnapshot snapshot{};
};

struct FlagSetterEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint8_t opcode = 0;
    uint8_t operand0 = 0;
    uint8_t operand1 = 0;
    uint8_t operand2 = 0;
    uint16_t afBefore = 0;
    uint16_t afAfter = 0;
    OpcodeWindow opcodes{};
};

struct Branch45F1Event {
    uint64_t step = 0;
    bool taken = false;
    uint16_t target = 0;
    uint16_t fallthrough = 0;
    uint8_t opcode = 0;
    uint8_t operand = 0;
    uint16_t af = 0;
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t ix = 0;
    uint16_t iy = 0;
    uint16_t sp = 0;
    BankState banks{};
    OpcodeWindow opcodes{};
    FlagSetterEvent flagSetter{};
    uint8_t c148 = 0;
    uint8_t c149 = 0;
    uint8_t c14a = 0;
    uint8_t c14b = 0;
    uint8_t c14e = 0;
    uint8_t c736 = 0;
    uint8_t c737 = 0;
    uint8_t c73c = 0;
    uint8_t c73d = 0;
    uint16_t c7b7 = 0;
    uint16_t c7b9 = 0;
    uint16_t c7cd = 0;
    uint16_t c7cf = 0;
    uint8_t scanline = 0;
};

struct MapperWriteEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t address = 0;
    uint8_t previousValue = 0;
    uint8_t value = 0;
    uint8_t c148 = 0;
    uint8_t c14a = 0;
    uint8_t scanline = 0;
    BankState before{};
    BankState after{};
    OpcodeWindow opcodes{};
};

struct PointerWriteEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t address = 0;
    uint8_t previousValue = 0;
    uint8_t value = 0;
    uint16_t af = 0;
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t ix = 0;
    uint16_t iy = 0;
    uint16_t sp = 0;
    uint8_t scanline = 0;
    BankState banks{};
    OpcodeWindow opcodes{};
};

struct ControlEvent {
    uint64_t step = 0;
    std::string kind;
    uint16_t pc = 0;
    uint16_t target = 0;
    uint16_t spBefore = 0;
    uint16_t spAfter = 0;
    CpuSnapshot snapshot{};
};

struct TimingSnapshot {
    uint64_t step = 0;
    uint64_t cpuCycles = 0;
    std::string cycleName;
    std::string label;
    uint16_t pc = 0;
    uint16_t pcAfter = 0;
    uint8_t statusValue = 0;
    bool hasStatusValue = false;
    uint8_t scanline = 0;
    uint32_t vdpDot = 0;
    bool hasVdpDot = false;
    uint8_t vCounter = 0;
    uint8_t hCounter = 0;
    bool framePending = false;
    bool linePending = false;
    bool irqAsserted = false;
    bool machineIrqPending = false;
    uint8_t reg0 = 0;
    uint8_t reg1 = 0;
    uint8_t reg10 = 0;
    uint8_t im = 0;
    bool ime = false;
    bool iff1 = false;
    bool iff2 = false;
};

struct OpcodeStats {
    uint64_t instructions = 0;
    uint64_t cycles = 0;
    std::map<std::string, uint64_t> opcodeCounts;
    std::map<std::string, uint64_t> opcodeCycles;
    std::map<std::string, uint64_t> groupCounts;
    std::map<std::string, uint64_t> groupCycles;
};

struct CycleTrace {
    bool valid = false;
    bool reached4B24 = false;
    bool cleared = false;
    bool reached4090 = false;
    uint64_t setStep = 0;
    uint64_t clearStep = 0;
    uint64_t setCpuCycles = 0;
    uint64_t clearCpuCycles = 0;
    uint64_t first4090Step = 0;
    uint64_t first4090CpuCycles = 0;
    CpuSnapshot setSnapshot{};
    CpuSnapshot clearSnapshot{};
    CpuSnapshot first4090Snapshot{};
    OpcodeStats stats{};
    OpcodeStats statsToFirst4090{};
    std::vector<BranchEvent> preSetBranches;
    std::vector<ControlEvent> preSetControl;
    std::vector<BranchEvent> branchHead;
    std::vector<BranchEvent> branches;
    std::vector<ControlEvent> controlHead;
    std::vector<ControlEvent> control;
    std::vector<TimingSnapshot> timingMilestones;
    std::vector<TimingSnapshot> timing;
};

template <typename T>
void pushTail(std::vector<T>& events, const T& event, std::size_t limit) {
    events.push_back(event);
    if (events.size() > limit) {
        events.erase(events.begin());
    }
}

bool isCycleTimingMilestone(const std::string& label) {
    return label == "pc_4500_before" ||
           label == "pc_4500_after" ||
           label == "ret_after_4500_before" ||
           label == "pc_4090_before" ||
           label == "pc_4095_before" ||
           label == "interrupt_entry_0038" ||
           label == "bf_status_read_before" ||
           label == "bf_status_read_after" ||
           label == "pc_4b24_clear_after";
}

void pushUniqueTimingMilestone(std::vector<TimingSnapshot>& milestones, const TimingSnapshot& timing) {
    const auto sameLabelCount = std::count_if(milestones.begin(), milestones.end(), [&](const TimingSnapshot& existing) {
        return existing.label == timing.label;
    });
    if ((timing.label == "pc_4095_before" ||
         timing.label == "interrupt_entry_0038" ||
         timing.label == "bf_status_read_before" ||
         timing.label == "bf_status_read_after") &&
        sameLabelCount >= 4) {
        return;
    }

    const auto duplicate = std::find_if(milestones.begin(), milestones.end(), [&](const TimingSnapshot& existing) {
        return existing.step == timing.step &&
               existing.cpuCycles == timing.cpuCycles &&
               existing.label == timing.label &&
               existing.pc == timing.pc &&
               existing.pcAfter == timing.pcAfter;
    });
    if (duplicate == milestones.end()) {
        milestones.push_back(timing);
    }
}

bool isWatchedPort(uint8_t port) {
    switch (port) {
    case 0x00u:
    case 0x7Eu:
    case 0x7Fu:
    case 0xBEu:
    case 0xBFu:
    case 0xDCu:
    case 0xDDu:
        return true;
    default:
        return false;
    }
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("unable to open ROM: " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string hex8(uint8_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<int>(value);
    return out.str();
}

std::string hex16(uint16_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<int>(value);
    return out.str();
}

std::string flagsSummary(uint8_t f) {
    std::ostringstream out;
    out << "S=" << ((f & 0x80u) != 0u ? 1 : 0)
        << " Z=" << ((f & 0x40u) != 0u ? 1 : 0)
        << " H=" << ((f & 0x10u) != 0u ? 1 : 0)
        << " PV=" << ((f & 0x04u) != 0u ? 1 : 0)
        << " N=" << ((f & 0x02u) != 0u ? 1 : 0)
        << " C=" << ((f & 0x01u) != 0u ? 1 : 0);
    return out.str();
}

std::string decodeBrief(uint8_t opcode, uint8_t operand0, uint8_t operand1, uint8_t operand2) {
    (void)operand2;
    std::ostringstream out;
    switch (opcode) {
    case 0x00u: return "NOP";
    case 0x10u: out << "DJNZ " << hex8(operand0); return out.str();
    case 0x18u: out << "JR " << hex8(operand0); return out.str();
    case 0x20u: out << "JR NZ," << hex8(operand0); return out.str();
    case 0x28u: out << "JR Z," << hex8(operand0); return out.str();
    case 0x30u: out << "JR NC," << hex8(operand0); return out.str();
    case 0x38u: out << "JR C," << hex8(operand0); return out.str();
    case 0x21u:
        out << "LD HL," << hex16(static_cast<uint16_t>(operand0 | (static_cast<uint16_t>(operand1) << 8u)));
        return out.str();
    case 0x32u:
        out << "LD (" << hex16(static_cast<uint16_t>(operand0 | (static_cast<uint16_t>(operand1) << 8u))) << "),A";
        return out.str();
    case 0x3Au:
        out << "LD A,(" << hex16(static_cast<uint16_t>(operand0 | (static_cast<uint16_t>(operand1) << 8u))) << ")";
        return out.str();
    case 0x3Eu: out << "LD A," << hex8(operand0); return out.str();
    case 0x77u: return "LD (HL),A";
    case 0x7Eu: return "LD A,(HL)";
    case 0xAFu: return "XOR A";
    case 0xA7u: return "AND A";
    case 0xB7u: return "OR A";
    case 0xBEu: return "CP (HL)";
    case 0xC9u: return "RET";
    case 0xCDu:
        out << "CALL " << hex16(static_cast<uint16_t>(operand0 | (static_cast<uint16_t>(operand1) << 8u)));
        return out.str();
    default:
        out << "OP " << hex8(opcode);
        if (opcode == 0xCBu || opcode == 0xDDu || opcode == 0xEDu || opcode == 0xFDu) {
            out << ' ' << hex8(operand0);
        }
        return out.str();
    }
}

bool updatesZFlag(uint8_t opcode, uint8_t operand0) {
    if (opcode == 0xCBu || opcode == 0xEDu) {
        return true;
    }
    if (opcode == 0xDDu || opcode == 0xFDu) {
        return operand0 == 0x34u || operand0 == 0x35u || operand0 == 0x86u ||
               operand0 == 0x8Eu || operand0 == 0x96u || operand0 == 0x9Eu ||
               operand0 == 0xA6u || operand0 == 0xAEu || operand0 == 0xB6u ||
               operand0 == 0xBEu || operand0 == 0xCBu;
    }
    if ((opcode >= 0x80u && opcode <= 0xBFu) || opcode == 0x04u || opcode == 0x05u ||
        opcode == 0x0Cu || opcode == 0x0Du || opcode == 0x14u || opcode == 0x15u ||
        opcode == 0x1Cu || opcode == 0x1Du || opcode == 0x24u || opcode == 0x25u ||
        opcode == 0x2Cu || opcode == 0x2Du || opcode == 0x34u || opcode == 0x35u ||
        opcode == 0x3Cu || opcode == 0x3Du || opcode == 0x27u || opcode == 0x2Fu ||
        opcode == 0x37u || opcode == 0x3Fu || opcode == 0xC6u || opcode == 0xCEu ||
        opcode == 0xD6u || opcode == 0xDEu || opcode == 0xE6u || opcode == 0xEEu ||
        opcode == 0xF6u || opcode == 0xFEu) {
        return true;
    }
    return false;
}

std::string opcodeKey(uint8_t opcode, uint8_t operand0, uint8_t operand1, uint8_t operand2) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setw(2) << std::setfill('0');
    if ((opcode == 0xDDu || opcode == 0xFDu) && operand0 == 0xCBu) {
        out << static_cast<int>(opcode) << "CB"
            << std::setw(2) << static_cast<int>(operand1)
            << std::setw(2) << static_cast<int>(operand2);
        return out.str();
    }
    if (opcode == 0xCBu || opcode == 0xEDu || opcode == 0xDDu || opcode == 0xFDu) {
        out << static_cast<int>(opcode)
            << std::setw(2) << static_cast<int>(operand0);
        return out.str();
    }
    out << static_cast<int>(opcode);
    return out.str();
}

std::string opcodeGroup(uint8_t opcode, uint8_t operand0) {
    if (opcode == 0xCBu) {
        return "CB";
    }
    if (opcode == 0xEDu) {
        return "ED";
    }
    if (opcode == 0xDDu && operand0 == 0xCBu) {
        return "DDCB";
    }
    if (opcode == 0xFDu && operand0 == 0xCBu) {
        return "FDCB";
    }
    if (opcode == 0xDDu) {
        return "DD";
    }
    if (opcode == 0xFDu) {
        return "FD";
    }
    return "base";
}

void recordOpcodeStats(OpcodeStats& stats,
                       uint8_t opcode,
                       uint8_t operand0,
                       uint8_t operand1,
                       uint8_t operand2,
                       uint32_t cycles) {
    const auto key = opcodeKey(opcode, operand0, operand1, operand2);
    const auto group = opcodeGroup(opcode, operand0);
    ++stats.instructions;
    stats.cycles += cycles;
    ++stats.opcodeCounts[key];
    stats.opcodeCycles[key] += cycles;
    ++stats.groupCounts[group];
    stats.groupCycles[group] += cycles;
}

OpcodeWindow captureOpcodeWindow(const GameGearMemoryMap& mem, uint16_t pc) {
    OpcodeWindow window{};
    window.start = static_cast<uint16_t>(pc - 8u);
    for (std::size_t i = 0; i < window.bytes.size(); ++i) {
        window.bytes[i] = mem.read(static_cast<uint16_t>(window.start + i));
    }
    return window;
}

void printOpcodeWindow(const OpcodeWindow& window) {
    for (std::size_t i = 0; i < window.bytes.size(); ++i) {
        std::cout << ' ' << hex16(static_cast<uint16_t>(window.start + i))
                  << ':' << hex8(window.bytes[i]);
    }
}

void analyzeOpcodeWindow(const OpcodeWindow& window) {
    bool any = false;
    for (std::size_t i = 0; i + 1 < window.bytes.size(); ++i) {
        const uint16_t addr = static_cast<uint16_t>(window.start + i);
        const uint8_t b0 = window.bytes[i];
        if (b0 == 0xFDu || b0 == 0xDDu) {
            const uint8_t op = window.bytes[i + 1];
            if (op == 0xCBu) {
                if (i + 3 < window.bytes.size()) {
                    const uint8_t disp = window.bytes[i + 2];
                    const uint8_t cbop = window.bytes[i + 3];
                    std::cout << "    decode:" << hex16(addr) << ":FDCB " << hex8(disp) << ':' << hex8(cbop)
                              << " -> bit/rot op on (IY+" << std::dec << static_cast<int8_t>(disp) << ")\n";
                    any = true;
                }
            } else {
                // common memory ops that become (IY+d)/(IX+d) when prefixed
                if (op == 0x34u) {
                    const uint8_t disp = (i + 2 < window.bytes.size()) ? window.bytes[i + 2] : 0u;
                    std::cout << "    decode:" << hex16(addr) << ":" << hex8(b0) << ':' << hex8(op)
                              << " -> INC (IY+" << std::dec << static_cast<int8_t>(disp) << ")\n";
                    any = true;
                } else if (op == 0x35u) {
                    const uint8_t disp = (i + 2 < window.bytes.size()) ? window.bytes[i + 2] : 0u;
                    std::cout << "    decode:" << hex16(addr) << ":" << hex8(b0) << ':' << hex8(op)
                              << " -> DEC (IY+" << std::dec << static_cast<int8_t>(disp) << ")\n";
                    any = true;
                } else if (op == 0x36u) {
                    const uint8_t disp = (i + 2 < window.bytes.size()) ? window.bytes[i + 2] : 0u;
                    const uint8_t imm = (i + 3 < window.bytes.size()) ? window.bytes[i + 3] : 0u;
                    std::cout << "    decode:" << hex16(addr) << ":" << hex8(b0) << ':' << hex8(op)
                              << " -> LD (IY+" << std::dec << static_cast<int8_t>(disp) << ")," << hex8(imm) << "\n";
                    any = true;
                } else if (op >= 0x70u && op <= 0x77u) {
                    const uint8_t disp = (i + 2 < window.bytes.size()) ? window.bytes[i + 2] : 0u;
                    std::cout << "    decode:" << hex16(addr) << ":" << hex8(b0) << ':' << hex8(op)
                              << " -> LD (IY+" << std::dec << static_cast<int8_t>(disp) << "),r\n";
                    any = true;
                } else {
                    // Generic prefix hint
                    std::cout << "    decode:" << hex16(addr) << ":" << hex8(b0) << ':' << hex8(op)
                              << " -> FD/DD prefix present (possible (IY/IX+d) access)\n";
                    any = true;
                }
            }
        }
    }
    if (!any) {
        std::cout << "    decode: no FD/DD patterns found in window\n";
    }
}

void printBankState(const BankState& banks) {
    if (!banks.available) {
        std::cout << " bank4000=n/a bank8000=n/a mapper_control=n/a";
        return;
    }
    std::cout << " bank4000=" << hex8(banks.registers[1])
              << " bank8000=" << hex8(banks.registers[2])
              << " bank0=" << hex8(banks.registers[0])
              << " mapper_control=" << hex8(banks.control);
}

template <typename T>
std::vector<T> takeTail(const std::vector<T>& events, std::size_t limit) {
    if (events.size() <= limit) {
        return events;
    }
    return std::vector<T>(events.end() - static_cast<std::ptrdiff_t>(limit), events.end());
}

bool isVdpDataPort(uint8_t port) {
    return (port & 0xC1u) == 0x80u;
}

bool isVdpControlPort(uint8_t port) {
    return (port & 0xC1u) == 0x81u;
}

bool isTracePc(uint16_t pc) {
    return pc == 0x44D0u || pc == 0x4500u || pc == 0x4B24u ||
           pc == 0x414Fu || pc == 0x4152u;
}

bool in45D0To45F1(uint16_t pc) {
    return pc >= 0x45D0u && pc <= 0x45F1u;
}

bool isPointerTrackedAddress(uint16_t address) {
    return (address >= 0xC148u && address <= 0xC14Eu) ||
           (address >= 0xC150u && address <= 0xC160u) ||
           (address >= 0xC730u && address <= 0xC740u) ||
           (address >= 0xC7B0u && address <= 0xC7C8u);
}

bool inMegaManRegion(uint16_t pc) {
    return pc >= 0x4000u && pc < 0x4C00u;
}

bool in4B24BranchNeighborhood(uint16_t pc) {
    return pc >= 0x4B10u && pc < 0x4B24u;
}

bool isConditionalRelativeBranch(uint8_t opcode) {
    return opcode == 0x10u || opcode == 0x20u || opcode == 0x28u ||
           opcode == 0x30u || opcode == 0x38u;
}

bool isConditionalAbsoluteControl(uint8_t opcode) {
    switch (opcode) {
    case 0xC2u:
    case 0xCAu:
    case 0xD2u:
    case 0xDAu:
    case 0xE2u:
    case 0xEAu:
    case 0xF2u:
    case 0xFAu:
    case 0xC4u:
    case 0xCCu:
    case 0xD4u:
    case 0xDCu:
    case 0xE4u:
    case 0xECu:
    case 0xF4u:
    case 0xFCu:
    case 0xC0u:
    case 0xC8u:
    case 0xD0u:
    case 0xD8u:
    case 0xE0u:
    case 0xE8u:
    case 0xF0u:
    case 0xF8u:
        return true;
    default:
        return false;
    }
}

bool isCallOpcode(uint8_t opcode) {
    return opcode == 0xCDu || opcode == 0xC4u || opcode == 0xCCu ||
           opcode == 0xD4u || opcode == 0xDCu || opcode == 0xE4u ||
           opcode == 0xECu || opcode == 0xF4u || opcode == 0xFCu;
}

bool isRetOpcode(uint8_t opcode) {
    return opcode == 0xC9u || opcode == 0xC0u || opcode == 0xC8u ||
           opcode == 0xD0u || opcode == 0xD8u || opcode == 0xE0u ||
           opcode == 0xE8u || opcode == 0xF0u || opcode == 0xF8u ||
           opcode == 0xD9u;
}

bool isBranchOpcode(uint8_t opcode) {
    return isConditionalRelativeBranch(opcode) || isConditionalAbsoluteControl(opcode) ||
           opcode == 0x18u || opcode == 0xC3u || opcode == 0xE9u || isCallOpcode(opcode) ||
           isRetOpcode(opcode);
}

void printUsage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " <rom.gg> [--steps N] [--press-start-at N] [--tail N]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    const std::filesystem::path romPath = argv[1];
    uint64_t maxSteps = 5'000'000u;
    std::optional<uint64_t> pressStartAt;
    std::size_t tailLimit = 80u;
    // optional: stop when CPU spins in the known C700 wait loop with C702 stuck high
    std::optional<uint64_t> stopOnC702SpinSamePcCount;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--steps" && i + 1 < argc) {
            maxSteps = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--press-start-at" && i + 1 < argc) {
            pressStartAt = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--tail" && i + 1 < argc) {
            tailLimit = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--stop-on-c702-spin") {
            // optionally accept an explicit same-pc-run threshold, otherwise default to 1000
            if (i + 1 < argc && std::isdigit(argv[i + 1][0])) {
                stopOnC702SpinSamePcCount = std::strtoull(argv[++i], nullptr, 10);
            } else {
                stopOnC702SpinSamePcCount = 1000u;
            }
        } else {
            printUsage(argv[0]);
            return 2;
        }
    }

    const auto rom = readFile(romPath);

    Z80Interpreter cpu;
    GameGearMemoryMap mem;
    GameGearVDP vdp;
    GameGearPSG psg;
    GameGearInput input;
    auto cart = createMapperFromRom(rom.data(), rom.size(), romPath);
    if (!cart) {
        std::cerr << "failed to create cartridge mapper\n";
        return 1;
    }
    auto* debugCart = dynamic_cast<GameGearCartridge*>(cart.get());

    mem.setCartridge(cart.get());
    mem.setInput(&input);
    mem.setPsg(&psg);
    mem.setVdp(&vdp);
    mem.reset();
    vdp.reset();
    psg.reset();
    input.reset();
    cpu.reset();

    std::vector<IoEvent> events;
    std::vector<MemEvent> memEvents;
    std::vector<RamWriteEvent> ramWrites;
    // Dedicated capture for writes to the C14A flag observed by the Mega Man wait loop
    std::vector<RamWriteEvent> c14aWrites;
    // Dedicated small ring for C700-C710 writes (inclusive)
    std::array<std::vector<RamWriteEvent>, 0x11> c700History;
    // Fast lookup of the last seen ram write per address
    std::map<uint16_t, RamWriteEvent> lastRamWrite;
    std::vector<C702WriteEvent> c702Writes;
    std::vector<C702ReadEvent> c702Reads;
    std::vector<InterruptEvent> interruptEvents;
    std::vector<VdpRegisterWriteEvent> vdpRegisterWrites;
    std::vector<IoEvent> bfStatusReads;
    std::vector<CpuSnapshot> tracePcHits;
    std::vector<BranchEvent> branchEvents;
    std::vector<BranchEvent> branchNeighborhoodEvents;
    std::vector<Branch45F1Event> branch45F1Events;
    std::vector<MapperWriteEvent> mapperWrites;
    std::vector<MapperWriteEvent> mapperWritesSinceLastC14AClear;
    std::vector<MapperWriteEvent> mapperWritesLastC14AClearToFinalSet;
    std::vector<PointerWriteEvent> pointerWrites;
    std::vector<ControlEvent> controlEvents;
    std::vector<VdpRegisterWriteEvent> vdpWritesBetweenClearAndSet;
    std::map<uint8_t, uint64_t> readCounts;
    std::map<uint8_t, uint64_t> writeCounts;
    std::map<uint8_t, uint8_t> lastRead;
    std::map<uint8_t, uint8_t> lastWrite;
    std::array<uint8_t, 0x0B> trackedVdpRegisters{};
    trackedVdpRegisters[0x02u] = 0xFFu;
    trackedVdpRegisters[0x05u] = 0xFFu;
    trackedVdpRegisters[0x06u] = 0xFFu;
    bool probeVdpLatchPending = false;
    uint8_t probeVdpCommandLow = 0;
    bool interruptRequested = false;
    bool interruptServedThisStep = false;
    uint8_t interruptVectorThisStep = 0;
    bool inInterruptRoutine = false;
    uint64_t lastInterruptStep = 0;
    uint64_t interruptProviderCalls = 0;
    uint64_t interruptProviderServed = 0;
    uint64_t currentStep = 0;
    uint64_t totalCpuCycles = 0;
    uint16_t currentInstructionPc = cpu.PC;
    uint8_t c702Value = mem.read(0xC702u);
    bool c702BecameNonzero = c702Value != 0u;
    bool c702EverWritten = false;
    bool c702EverBecameNonzero = c702BecameNonzero;
    bool c702ClearedAfterNonzero = false;
    bool c702RepeatedNonzeroWrite = false;
    bool c702RepeatedZeroWrite = false;
    std::optional<uint64_t> firstC702NonzeroStep;
    std::optional<uint64_t> firstC702ClearAfterNonzeroStep;
    std::optional<uint64_t> lastC14AClearStep;
    std::optional<uint64_t> finalC14ASetStep;
    std::optional<IoEvent> firstBfStatusReadAfterC702Nonzero;
    std::optional<IoEvent> firstBfStatusReadAfterInterruptAfterC702Nonzero;
    CycleTrace activeCycle;
    CycleTrace lastSuccessfulCycle;
    CycleTrace finalCycle;
    OpcodeStats betweenSuccessfulClearAndFinalSetStats;
    std::map<uint16_t, uint64_t> tracePcCounts;
    std::vector<TimingSnapshot> timingSnapshots;
    std::vector<TimingSnapshot> bfTimingSinceLastClear;
    bool collectBfTimingSinceLastClear = false;
    bool collectVdpWritesBetweenClearAndSet = false;
    bool collectBetweenSuccessfulClearAndFinalSetStats = false;
    // open a small on-disk log so we can observe IY/IX changes as they happen
    std::ofstream iyChangeLog("/tmp/iy_changes.log", std::ios::trunc);
    iyChangeLog << "step|pc|op0|op1|op2|op3|oldIY|newIY|oldIX|newIX|af|bc|de|hl|sp|scanline|machine_irq_pending|in_isr" << std::endl;
    std::ofstream pc4b7bLog("/tmp/pc_4b7b.log", std::ios::trunc);
    pc4b7bLog << "step|pc|op0|op1|op2|op3|iy|ix|af|bc|de|hl|sp|scanline|machine_irq_pending|in_isr" << std::endl;
    std::ofstream pc45f1Log("/tmp/pc45f1_trace.log", std::ios::trunc);
    pc45f1Log << "step|bank4000|bank8000|bank0|mapper_control|op0|op1|af|flags|taken|flag_pc|flag_op0|flag_op1|flag_af_before|flag_af_after|bc|de|hl|ix|iy|sp|scanline|c148|c149|c14a|c14b|c14e|c736|c737|c73c|c73d|c7b7|c7b9|c7cd|c7cf" << std::endl;
    std::ofstream pc45rangeLog("/tmp/pc45d0_45f1_trace.log", std::ios::trunc);
    pc45rangeLog << "step|pc|op0|op1|op2|op3|af|bc|de|hl|ix|iy|sp|bank4000|bank8000|bank0|mapper_control|scanline|c148|c149|c14a|c14b|c14e|c736|c737|c73c|c73d|c7b7|c7b9|c7cd|c7cf|hl_byte_before|hl_minus1_byte_before" << std::endl;
    std::ofstream pointerWriteLog("/tmp/pointer_ram_writes.log", std::ios::trunc);
    pointerWriteLog << "step|pc|addr|prev|value|af|bc|de|hl|ix|iy|sp|bank4000|bank8000|bank0|mapper_control|scanline" << std::endl;
    uint64_t branch45F1Count = 0;
    std::optional<Branch45F1Event> branch45F1FirstSuccess;
    std::optional<Branch45F1Event> branch45F1FinalDivergent;

    auto bankState = [&]() {
        BankState state{};
        if (debugCart != nullptr) {
            state.available = true;
            state.registers = debugCart->bankRegisters();
            state.control = debugCart->controlRegister();
        }
        return state;
    };
    auto pushMapperWrite = [&](MapperWriteEvent event) {
        pushTail(mapperWrites, event, tailLimit);
        if (lastC14AClearStep.has_value()) {
            mapperWritesSinceLastC14AClear.push_back(event);
            if (mapperWritesSinceLastC14AClear.size() > tailLimit) {
                mapperWritesSinceLastC14AClear.erase(mapperWritesSinceLastC14AClear.begin());
            }
        }
    };
    auto pushPointerWrite = [&](PointerWriteEvent event) {
        pushTail(pointerWrites, event, tailLimit);
        pointerWriteLog << event.step << '|'
                        << hex16(event.pc) << '|'
                        << hex16(event.address) << '|'
                        << hex8(event.previousValue) << '|'
                        << hex8(event.value) << '|'
                        << hex16(event.af) << '|'
                        << hex16(event.bc) << '|'
                        << hex16(event.de) << '|'
                        << hex16(event.hl) << '|'
                        << hex16(event.ix) << '|'
                        << hex16(event.iy) << '|'
                        << hex16(event.sp) << '|'
                        << (event.banks.available ? hex8(event.banks.registers[1]) : std::string("n/a")) << '|'
                        << (event.banks.available ? hex8(event.banks.registers[2]) : std::string("n/a")) << '|'
                        << (event.banks.available ? hex8(event.banks.registers[0]) : std::string("n/a")) << '|'
                        << (event.banks.available ? hex8(event.banks.control) : std::string("n/a")) << '|'
                        << static_cast<int>(event.scanline)
                        << '\n';
    };
    auto snapshotCpu = [&](uint16_t pc,
                           uint8_t before,
                           uint8_t after,
                           bool includeMemoryRanges = false,
                           bool includeOpcodeWindow = false) {
        // helper to read an inclusive range from memory and return a vector
        auto readRange = [&](int start, int end) {
            std::vector<uint8_t> out;
            if (end < start) return out;
            const int s = std::max(0, start);
            const int e = std::min(0xFFFF, end);
            out.reserve(static_cast<std::size_t>(e - s + 1));
            for (int a = s; a <= e; ++a) {
                out.push_back(mem.read(static_cast<uint16_t>(a)));
            }
            return out;
        };

        const uint16_t ix = cpu.IX;
        const uint16_t iy = cpu.IY;
        const auto r_c700 = includeMemoryRanges ? readRange(0xC700, 0xC720) : std::vector<uint8_t>{};
        const auto r_c780 = includeMemoryRanges ? readRange(0xC780, 0xC7C0) : std::vector<uint8_t>{};
        const auto r_c880 = includeMemoryRanges ? readRange(0xC880, 0xC8A0) : std::vector<uint8_t>{};
        const auto r_c4e0 = includeMemoryRanges ? readRange(0xC4E0, 0xC720) : std::vector<uint8_t>{};
        const auto r_ix = includeMemoryRanges ? readRange(static_cast<int>(ix) - 16, static_cast<int>(ix) + 32) : std::vector<uint8_t>{};
        const auto r_iy = includeMemoryRanges ? readRange(static_cast<int>(iy) - 16, static_cast<int>(iy) + 32) : std::vector<uint8_t>{};

        return CpuSnapshot{
            currentStep,
            pc,
            cpu.AF,
            cpu.BC,
            cpu.DE,
            cpu.HL,
            cpu.IX,
            cpu.IY,
            cpu.SP,
            bankState(),
            before,
            after,
            includeOpcodeWindow ? captureOpcodeWindow(mem, pc) : OpcodeWindow{},
            r_c700,
            r_c780,
            r_c880,
            r_c4e0,
            r_ix,
            r_iy,
        };
    };
    auto pushCycleBranch = [&](CycleTrace& cycle, const BranchEvent& event) {
        if (cycle.branchHead.size() < 80u) {
            cycle.branchHead.push_back(event);
        }
        pushTail(cycle.branches, event, 80u);
    };
    auto pushCycleControl = [&](CycleTrace& cycle, const ControlEvent& event) {
        if (cycle.controlHead.size() < 80u) {
            cycle.controlHead.push_back(event);
        }
        pushTail(cycle.control, event, 80u);
    };
    auto captureTiming = [&](std::string cycleName,
                             std::string label,
                             uint16_t pc,
                             uint16_t pcAfter,
                             std::optional<uint8_t> statusValue = std::nullopt) {
        return TimingSnapshot{
            currentStep,
            totalCpuCycles,
            std::move(cycleName),
            std::move(label),
            pc,
            pcAfter,
            statusValue.value_or(0u),
            statusValue.has_value(),
            vdp.currentScanline(),
            0u,
            false,
            vdp.readVCounter(),
            vdp.readHCounter(),
            vdp.isFrameInterruptPending(),
            vdp.isLineInterruptPending(),
            vdp.isIrqAsserted(),
            interruptRequested,
            trackedVdpRegisters[0x00u],
            trackedVdpRegisters[0x01u],
            trackedVdpRegisters[0x0Au],
            cpu.interruptMode_,
            cpu.IME,
            cpu.IFF1,
            cpu.IFF2,
        };
    };
    auto recordTiming = [&](const TimingSnapshot& timing) {
        pushTail(timingSnapshots, timing, tailLimit);
        if (activeCycle.valid) {
            if (isCycleTimingMilestone(timing.label)) {
                pushUniqueTimingMilestone(activeCycle.timingMilestones, timing);
            }
            pushTail(activeCycle.timing, timing, 120u);
        }
    };

    auto record = [&](char kind, uint8_t port, uint8_t value) {
        if (!isWatchedPort(port)) {
            return;
        }
        if (kind == 'R') {
            ++readCounts[port];
            lastRead[port] = value;
        } else {
            ++writeCounts[port];
            lastWrite[port] = value;
        }
        const IoEvent event{
            currentStep,
            currentInstructionPc,
            kind,
            port,
            value,
            vdp.currentScanline(),
            c702BecameNonzero,
            inInterruptRoutine,
        };
        pushTail(events, event, tailLimit);
        if (kind == 'R' && isVdpControlPort(port)) {
            pushTail(bfStatusReads, event, tailLimit);
            if (c702BecameNonzero && !firstBfStatusReadAfterC702Nonzero.has_value()) {
                firstBfStatusReadAfterC702Nonzero = event;
            }
            if (c702BecameNonzero && interruptProviderServed > 0u &&
                currentStep >= lastInterruptStep &&
                !firstBfStatusReadAfterInterruptAfterC702Nonzero.has_value()) {
                firstBfStatusReadAfterInterruptAfterC702Nonzero = event;
            }
        }
    };
    auto clearProbeVdpLatch = [&]() {
        probeVdpLatchPending = false;
    };
    auto recordVdpRegisterWrite = [&](uint8_t reg, uint8_t value) {
        if (reg < trackedVdpRegisters.size()) {
            trackedVdpRegisters[reg] = value;
        }
        if (reg != 0x00u && reg != 0x01u && reg != 0x0Au) {
            return;
        }
        const bool ie1 = (trackedVdpRegisters[0x00u] & 0x10u) != 0u;
        const bool ie0 = (trackedVdpRegisters[0x01u] & 0x20u) != 0u;
        pushTail(vdpRegisterWrites,
                 VdpRegisterWriteEvent{
                     currentStep,
                     currentInstructionPc,
                     reg,
                     value,
                     vdp.currentScanline(),
                     ie0,
                     ie1,
                     trackedVdpRegisters[0x0Au],
                 },
                 tailLimit);
        if (collectVdpWritesBetweenClearAndSet) {
            pushTail(vdpWritesBetweenClearAndSet,
                     VdpRegisterWriteEvent{
                         currentStep,
                         currentInstructionPc,
                         reg,
                         value,
                         vdp.currentScanline(),
                         ie0,
                         ie1,
                         trackedVdpRegisters[0x0Au],
                     },
                     tailLimit);
        }
    };
    auto observeIoAccess = [&](char kind, uint8_t port, uint8_t value) {
        if (kind == 'W' && isVdpControlPort(port)) {
            if (!probeVdpLatchPending) {
                probeVdpCommandLow = value;
                probeVdpLatchPending = true;
            } else {
                probeVdpLatchPending = false;
                const auto command = static_cast<uint8_t>((value >> 6) & 0x03u);
                if (command == 0x02u) {
                    recordVdpRegisterWrite(static_cast<uint8_t>(value & 0x0Fu), probeVdpCommandLow);
                }
            }
        } else if ((kind == 'R' && (isVdpControlPort(port) || isVdpDataPort(port))) ||
                   (kind == 'W' && isVdpDataPort(port))) {
            clearProbeVdpLatch();
        }
    };
    auto recordMemWrite = [&](uint16_t address, uint8_t value) {
        const bool watched =
            (address >= 0xC700u && address <= 0xC720u) ||
            (address >= 0xC780u && address <= 0xC7C0u) ||
            (address >= 0xC880u && address <= 0xC8A0u) ||
            (address >= 0xC4E0u && address <= 0xC720u);
        if (!watched) {
            return;
        }
        pushTail(memEvents, MemEvent{
            currentStep,
            currentInstructionPc,
            address,
            value,
            vdp.currentScanline(),
            captureOpcodeWindow(mem, currentInstructionPc),
        }, tailLimit);
    };

    auto recordRamWrite = [&](uint16_t address, uint8_t previousValue, uint8_t newValue) {
        // Only track the targeted range explicitly: $C500-$C720
        if (address < 0xC500u || address > 0xC720u) return;
        RamWriteEvent ev{};
        ev.step = currentStep;
        ev.pc = currentInstructionPc;
        ev.address = address;
        ev.previousValue = previousValue;
        ev.value = newValue;
        ev.scanline = vdp.currentScanline();
        ev.opcodes = captureOpcodeWindow(mem, currentInstructionPc);
        ev.af = cpu.AF;
        ev.bc = cpu.BC;
        ev.de = cpu.DE;
        ev.hl = cpu.HL;
        ev.ix = cpu.IX;
        ev.iy = cpu.IY;
        ev.sp = cpu.SP;
        ev.machine_irq_pending = interruptRequested;
        ev.vdp_frame_pending = vdp.isFrameInterruptPending();
        ev.vdp_line_pending = vdp.isLineInterruptPending();
        ramWrites.push_back(ev);
        // Keep a small dedicated history for C700-C710
        if (address >= 0xC700u && address <= 0xC710u) {
            const std::size_t idx = static_cast<std::size_t>(address - 0xC700u);
            pushTail(c700History[idx], ev, 512u);
        }
        // Update fast last-write map for fallback attribution
        lastRamWrite[address] = ev;
    };
    auto recordC702Read = [&](uint8_t value) {
        pushTail(c702Reads,
                 C702ReadEvent{
                     currentStep,
                     currentInstructionPc,
                     value,
                     vdp.currentScanline(),
                 },
                 tailLimit);
    };
    auto recordC702Write = [&](uint8_t value) {
        const uint8_t previousValue = c702Value;
        c702EverWritten = true;
        if (previousValue != 0u && value == 0u) {
            c702ClearedAfterNonzero = true;
            if (!firstC702ClearAfterNonzeroStep.has_value()) {
                firstC702ClearAfterNonzeroStep = currentStep;
            }
        }
        if (previousValue != 0u && value != 0u) {
            c702RepeatedNonzeroWrite = true;
        }
        if (previousValue == 0u && value == 0u) {
            c702RepeatedZeroWrite = true;
        }
        if (previousValue == 0u && value != 0u && !firstC702NonzeroStep.has_value()) {
            firstC702NonzeroStep = currentStep;
        }
        if (value != 0u) {
            c702BecameNonzero = true;
            c702EverBecameNonzero = true;
        }
        c702Value = value;
        pushTail(c702Writes,
                 C702WriteEvent{
                     currentStep,
                     currentInstructionPc,
                     value,
                     previousValue,
                     vdp.currentScanline(),
                     captureOpcodeWindow(mem, currentInstructionPc),
                 },
                 tailLimit);
    };

    cpu.setMemoryInterface(
        [&](uint16_t address) {
            const auto value = mem.read(address);
            if (address == 0xC702u) {
                recordC702Read(value);
            }
            // record reads to the C14A wait flag so we can see who is polling it
            if (address == 0xC14Au) {
                pushTail(memEvents,
                         MemEvent{currentStep, currentInstructionPc, address, value, vdp.currentScanline(), captureOpcodeWindow(mem, currentInstructionPc)},
                         tailLimit);
            }
            return value;
        },
        [&](uint16_t address, uint8_t value) {
            // capture previous value before the write
            const uint8_t previous = mem.read(address);
            const bool mapperWrite = address >= 0xFFFCu;
            const auto mapperBefore = mapperWrite ? bankState() : BankState{};
            mem.write(address, value);
            if (mapperWrite) {
                pushMapperWrite(MapperWriteEvent{
                    currentStep,
                    currentInstructionPc,
                    address,
                    previous,
                    value,
                    mem.read(0xC148u),
                    mem.read(0xC14Au),
                    vdp.currentScanline(),
                    mapperBefore,
                    bankState(),
                    captureOpcodeWindow(mem, currentInstructionPc),
                });
            }
            recordMemWrite(address, value);
            // record full metadata for writes to $C500-$C720
            if (address >= 0xC500u && address <= 0xC720u) {
                recordRamWrite(address, previous, value);
            }
            if (isPointerTrackedAddress(address)) {
                pushPointerWrite(PointerWriteEvent{
                    currentStep,
                    currentInstructionPc,
                    address,
                    previous,
                    value,
                    cpu.AF,
                    cpu.BC,
                    cpu.DE,
                    cpu.HL,
                    cpu.IX,
                    cpu.IY,
                    cpu.SP,
                    vdp.currentScanline(),
                    bankState(),
                    captureOpcodeWindow(mem, currentInstructionPc),
                });
            }
            // also record writes to the specific C14A flag (Mega Man wait)
            if (address == 0xC14Au) {
                if (value == 0u) {
                    lastC14AClearStep = currentStep;
                    mapperWritesSinceLastC14AClear.clear();
                } else if (value == 0xFFu) {
                    finalC14ASetStep = currentStep;
                    mapperWritesLastC14AClearToFinalSet = mapperWritesSinceLastC14AClear;
                }
                RamWriteEvent ev{};
                ev.step = currentStep;
                ev.pc = currentInstructionPc;
                ev.address = address;
                ev.previousValue = previous;
                ev.value = value;
                ev.scanline = vdp.currentScanline();
                ev.opcodes = captureOpcodeWindow(mem, currentInstructionPc);
                ev.af = cpu.AF;
                ev.bc = cpu.BC;
                ev.de = cpu.DE;
                ev.hl = cpu.HL;
                ev.ix = cpu.IX;
                ev.iy = cpu.IY;
                ev.sp = cpu.SP;
                ev.machine_irq_pending = interruptRequested;
                ev.vdp_frame_pending = vdp.isFrameInterruptPending();
                ev.vdp_line_pending = vdp.isLineInterruptPending();
                c14aWrites.push_back(ev);
                // also surface the write into generic memEvents for the recent_ram_writes list
                pushTail(memEvents,
                         MemEvent{currentStep, currentInstructionPc, address, value, vdp.currentScanline(), captureOpcodeWindow(mem, currentInstructionPc)},
                         tailLimit);
                lastRamWrite[address] = ev;
            }
            if (address == 0xC702u) {
                recordC702Write(value);
            }
        });
    cpu.setIoInterface(
        [&](uint8_t port) {
            std::optional<TimingSnapshot> bfBeforeRead;
            if (isVdpControlPort(port)) {
                bfBeforeRead = captureTiming(activeCycle.valid ? "after_4500" : "outside_cycle",
                                             "bf_status_read_before",
                                             currentInstructionPc,
                                             currentInstructionPc);
            }
            const auto value = mem.readIoPort(port);
            observeIoAccess('R', port, value);
            record('R', port, value);
            if (isVdpControlPort(port)) {
                if (bfBeforeRead.has_value()) {
                    recordTiming(*bfBeforeRead);
                    if (collectBfTimingSinceLastClear) {
                        bfTimingSinceLastClear.push_back(*bfBeforeRead);
                    }
                }
                auto bfAfterRead = captureTiming(activeCycle.valid ? "after_4500" : "outside_cycle",
                                                 "bf_status_read_after",
                                                 currentInstructionPc,
                                                 currentInstructionPc,
                                                 value);
                recordTiming(bfAfterRead);
                if (collectBfTimingSinceLastClear) {
                    bfTimingSinceLastClear.push_back(bfAfterRead);
                }
            }
            return value;
        },
        [&](uint8_t port, uint8_t value) {
            mem.writeIoPort(port, value);
            observeIoAccess('W', port, value);
            record('W', port, value);
        });
    cpu.setInterruptRequestProvider([&]() -> std::optional<uint8_t> {
        ++interruptProviderCalls;
        if (interruptRequested) {
            interruptRequested = false;
            ++interruptProviderServed;
            interruptServedThisStep = true;
            interruptVectorThisStep = 0u;
            return static_cast<uint8_t>(0u);
        }
        return std::nullopt;
    });

    std::map<uint16_t, uint64_t> pcCounts;
    std::array<uint16_t, 16> recentPcs{};
    std::size_t recentPcIndex = 0;
    uint16_t lastPc = 0;
    uint64_t samePcRun = 0;

    uint16_t prevIY = cpu.IY;
    uint16_t prevIX = cpu.IX;
    FlagSetterEvent lastFlagSetter{};
    for (currentStep = 0; currentStep < maxSteps; ++currentStep) {
        if (pressStartAt.has_value() && currentStep == *pressStartAt) {
            input.setLogicalButtons(BMMQ::inputButtonMask(BMMQ::InputButton::Meta2));
        }

        lastPc = cpu.PC;
        currentInstructionPc = cpu.PC;
        interruptServedThisStep = false;
        ++pcCounts[cpu.PC];
        recentPcs[recentPcIndex++ % recentPcs.size()] = cpu.PC;
        const uint8_t c702BeforeStep = c702Value;
        const uint8_t opcodeAtPc = mem.read(cpu.PC);
        const uint8_t nextOpcodeByte = mem.read(static_cast<uint16_t>(cpu.PC + 1u));
        const uint8_t thirdOpcodeByte = mem.read(static_cast<uint16_t>(cpu.PC + 2u));
        const uint8_t fourthOpcodeByte = mem.read(static_cast<uint16_t>(cpu.PC + 3u));
        const uint16_t preStepSp = cpu.SP;
        const uint64_t preInstructionCpuCycles = totalCpuCycles;
        const uint16_t afBeforeStep = cpu.AF;
        const auto preRangeBanks = in45D0To45F1(lastPc) ? std::optional<BankState>(bankState()) : std::nullopt;
        const auto pre45F1Banks = lastPc == 0x45F1u ? std::optional<BankState>(bankState()) : std::nullopt;
        const auto pre45F1Opcodes = lastPc == 0x45F1u ? std::optional<OpcodeWindow>(captureOpcodeWindow(mem, lastPc)) : std::nullopt;
        if (in45D0To45F1(lastPc) && preRangeBanks.has_value()) {
            const uint8_t hlByteBefore = mem.read(cpu.HL);
            const uint8_t hlMinus1ByteBefore = mem.read(static_cast<uint16_t>(cpu.HL - 1u));
            pc45rangeLog << currentStep << '|'
                         << hex16(lastPc) << '|'
                         << hex8(opcodeAtPc) << '|'
                         << hex8(nextOpcodeByte) << '|'
                         << hex8(thirdOpcodeByte) << '|'
                         << hex8(fourthOpcodeByte) << '|'
                         << hex16(cpu.AF) << '|'
                         << hex16(cpu.BC) << '|'
                         << hex16(cpu.DE) << '|'
                         << hex16(cpu.HL) << '|'
                         << hex16(cpu.IX) << '|'
                         << hex16(cpu.IY) << '|'
                         << hex16(preStepSp) << '|'
                         << hex8(preRangeBanks->registers[1]) << '|'
                         << hex8(preRangeBanks->registers[2]) << '|'
                         << hex8(preRangeBanks->registers[0]) << '|'
                         << hex8(preRangeBanks->control) << '|'
                         << static_cast<int>(vdp.currentScanline()) << '|'
                         << hex8(mem.read(0xC148u)) << '|'
                         << hex8(mem.read(0xC149u)) << '|'
                         << hex8(mem.read(0xC14Au)) << '|'
                         << hex8(mem.read(0xC14Bu)) << '|'
                         << hex8(mem.read(0xC14Eu)) << '|'
                         << hex8(mem.read(0xC736u)) << '|'
                         << hex8(mem.read(0xC737u)) << '|'
                         << hex8(mem.read(0xC73Cu)) << '|'
                         << hex8(mem.read(0xC73Du)) << '|'
                         << hex16(static_cast<uint16_t>(mem.read(0xC7B7u) |
                                                       (static_cast<uint16_t>(mem.read(0xC7B8u)) << 8u))) << '|'
                         << hex16(static_cast<uint16_t>(mem.read(0xC7B9u) |
                                                       (static_cast<uint16_t>(mem.read(0xC7BAu)) << 8u))) << '|'
                         << hex16(static_cast<uint16_t>(mem.read(0xC7CDu) |
                                                       (static_cast<uint16_t>(mem.read(0xC7CEu)) << 8u))) << '|'
                         << hex16(static_cast<uint16_t>(mem.read(0xC7CFu) |
                                                       (static_cast<uint16_t>(mem.read(0xC7D0u)) << 8u))) << '|'
                         << hex8(hlByteBefore) << '|'
                         << hex8(hlMinus1ByteBefore)
                         << '\n';
        }
        std::optional<TimingSnapshot> preStepTiming;
        if (lastPc == 0x4090u || lastPc == 0x4095u ||
            (lastPc == 0x4516u && activeCycle.valid) || lastPc == 0x4500u) {
            preStepTiming = captureTiming(activeCycle.valid ? "after_4500" : "outside_cycle",
                                          "pc_before_execute",
                                          lastPc,
                                          lastPc);
            if (lastPc != 0x4500u) {
                preStepTiming->label = lastPc == 0x4090u ? "pc_4090_before" :
                                       lastPc == 0x4095u ? "pc_4095_before" :
                                                           "ret_after_4500_before";
                recordTiming(*preStepTiming);
            }
        }
        const auto cycles = cpu.step();
        totalCpuCycles += cycles;
        const auto postStepSnapshot = snapshotCpu(lastPc, c702BeforeStep, c702Value);
        if (lastPc == 0x45F1u && pre45F1Banks.has_value() && pre45F1Opcodes.has_value()) {
            const uint16_t fallthrough = static_cast<uint16_t>(lastPc + 2u);
            const uint16_t target = static_cast<uint16_t>(fallthrough + static_cast<int8_t>(nextOpcodeByte));
            Branch45F1Event event{
                currentStep,
                cpu.PC == target,
                target,
                fallthrough,
                opcodeAtPc,
                nextOpcodeByte,
                afBeforeStep,
                cpu.BC,
                cpu.DE,
                cpu.HL,
                cpu.IX,
                cpu.IY,
                preStepSp,
                *pre45F1Banks,
                *pre45F1Opcodes,
                lastFlagSetter,
                mem.read(0xC148u),
                mem.read(0xC149u),
                mem.read(0xC14Au),
                mem.read(0xC14Bu),
                mem.read(0xC14Eu),
                mem.read(0xC736u),
                mem.read(0xC737u),
                mem.read(0xC73Cu),
                mem.read(0xC73Du),
                static_cast<uint16_t>(mem.read(0xC7B7u) |
                                      (static_cast<uint16_t>(mem.read(0xC7B8u)) << 8u)),
                static_cast<uint16_t>(mem.read(0xC7B9u) |
                                      (static_cast<uint16_t>(mem.read(0xC7BAu)) << 8u)),
                static_cast<uint16_t>(mem.read(0xC7CDu) |
                                      (static_cast<uint16_t>(mem.read(0xC7CEu)) << 8u)),
                static_cast<uint16_t>(mem.read(0xC7CFu) |
                                      (static_cast<uint16_t>(mem.read(0xC7D0u)) << 8u)),
                vdp.currentScanline(),
            };
            ++branch45F1Count;
            if (!event.taken && event.banks.available && event.banks.registers[2] == 0x19u &&
                !branch45F1FirstSuccess.has_value()) {
                branch45F1FirstSuccess = event;
            }
            if (event.taken && event.banks.available && event.banks.registers[2] == 0x12u) {
                branch45F1FinalDivergent = event;
            }
            pc45f1Log << event.step << '|'
                      << (event.banks.available ? hex8(event.banks.registers[1]) : std::string("n/a")) << '|'
                      << (event.banks.available ? hex8(event.banks.registers[2]) : std::string("n/a")) << '|'
                      << (event.banks.available ? hex8(event.banks.registers[0]) : std::string("n/a")) << '|'
                      << (event.banks.available ? hex8(event.banks.control) : std::string("n/a")) << '|'
                      << hex8(event.opcode) << '|' << hex8(event.operand) << '|'
                      << hex16(event.af) << '|' << flagsSummary(static_cast<uint8_t>(event.af & 0x00FFu)) << '|'
                      << (event.taken ? "yes" : "no") << '|'
                      << hex16(event.flagSetter.pc) << '|' << hex8(event.flagSetter.opcode) << '|'
                      << hex8(event.flagSetter.operand0) << '|'
                      << hex16(event.flagSetter.afBefore) << '|' << hex16(event.flagSetter.afAfter) << '|'
                      << hex16(event.bc) << '|' << hex16(event.de) << '|' << hex16(event.hl) << '|'
                      << hex16(event.ix) << '|' << hex16(event.iy) << '|' << hex16(event.sp) << '|'
                      << static_cast<int>(event.scanline) << '|'
                      << hex8(event.c148) << '|' << hex8(event.c149) << '|' << hex8(event.c14a) << '|'
                      << hex8(event.c14b) << '|' << hex8(event.c14e) << '|'
                      << hex8(event.c736) << '|' << hex8(event.c737) << '|'
                      << hex8(event.c73c) << '|' << hex8(event.c73d) << '|'
                      << hex16(event.c7b7) << '|' << hex16(event.c7b9) << '|'
                      << hex16(event.c7cd) << '|' << hex16(event.c7cf) << '\n';
            pushTail(branch45F1Events, event, tailLimit);
        }
        if (updatesZFlag(opcodeAtPc, nextOpcodeByte)) {
            lastFlagSetter = FlagSetterEvent{
                currentStep,
                lastPc,
                opcodeAtPc,
                nextOpcodeByte,
                thirdOpcodeByte,
                fourthOpcodeByte,
                afBeforeStep,
                cpu.AF,
                captureOpcodeWindow(mem, lastPc),
            };
        }
        // record whenever PC==0x4B7B so we can inspect IY at each execution
        if (lastPc == 0x4B7Bu) {
            const uint8_t o0 = opcodeAtPc;
            const uint8_t o1 = nextOpcodeByte;
            const uint8_t o2 = thirdOpcodeByte;
            const uint8_t o3 = fourthOpcodeByte;
            pc4b7bLog << postStepSnapshot.step << '|' << hex16(postStepSnapshot.pc) << '|' \
                      << hex8(o0) << '|' << hex8(o1) << '|' << hex8(o2) << '|' << hex8(o3) << '|' \
                      << hex16(postStepSnapshot.iy) << '|' << hex16(postStepSnapshot.ix) << '|' \
                      << hex16(postStepSnapshot.af) << '|' << hex16(postStepSnapshot.bc) << '|' \
                      << hex16(postStepSnapshot.de) << '|' << hex16(postStepSnapshot.hl) << '|' \
                      << hex16(postStepSnapshot.sp) << '|' << static_cast<int>(vdp.currentScanline()) << '|' \
                      << (interruptRequested ? "yes" : "no") << '|' << (inInterruptRoutine ? "yes" : "no") << std::endl;
            pc4b7bLog.flush();
        }
        // detect IY/IX changes and write immediate probe log entries
        if (postStepSnapshot.iy != prevIY || postStepSnapshot.ix != prevIX) {
            const uint8_t o0 = opcodeAtPc;
            const uint8_t o1 = nextOpcodeByte;
            const uint8_t o2 = thirdOpcodeByte;
            const uint8_t o3 = fourthOpcodeByte;
            iyChangeLog << postStepSnapshot.step << '|' << hex16(postStepSnapshot.pc) << '|' \
                        << hex8(o0) << '|' << hex8(o1) << '|' << hex8(o2) << '|' << hex8(o3) << '|' \
                        << hex16(prevIY) << '|' << hex16(postStepSnapshot.iy) << '|' \
                        << hex16(prevIX) << '|' << hex16(postStepSnapshot.ix) << '|' \
                        << hex16(postStepSnapshot.af) << '|' << hex16(postStepSnapshot.bc) << '|' \
                        << hex16(postStepSnapshot.de) << '|' << hex16(postStepSnapshot.hl) << '|' \
                        << hex16(postStepSnapshot.sp) << '|' << static_cast<int>(vdp.currentScanline()) << '|' \
                        << (interruptRequested ? "yes" : "no") << '|' << (inInterruptRoutine ? "yes" : "no") << std::endl;
            iyChangeLog.flush();
            prevIY = postStepSnapshot.iy;
            prevIX = postStepSnapshot.ix;
        }
        if (collectBetweenSuccessfulClearAndFinalSetStats && lastPc != 0x4500u) {
            recordOpcodeStats(betweenSuccessfulClearAndFinalSetStats,
                              opcodeAtPc,
                              nextOpcodeByte,
                              thirdOpcodeByte,
                              fourthOpcodeByte,
                              cycles);
        }
        if (isTracePc(lastPc)) {
            ++tracePcCounts[lastPc];
            pushTail(tracePcHits, postStepSnapshot, tailLimit);
        }
        if (lastPc == 0x4500u) {
            if (activeCycle.valid && !activeCycle.reached4B24) {
                finalCycle = activeCycle;
            }
            if (collectVdpWritesBetweenClearAndSet) {
                collectVdpWritesBetweenClearAndSet = false;
            }
            if (collectBetweenSuccessfulClearAndFinalSetStats) {
                collectBetweenSuccessfulClearAndFinalSetStats = false;
            }
            activeCycle = CycleTrace{};
            activeCycle.valid = true;
            activeCycle.setStep = currentStep;
            activeCycle.setCpuCycles = preInstructionCpuCycles;
            activeCycle.setSnapshot = postStepSnapshot;
            activeCycle.preSetBranches = takeTail(branchEvents, 30u);
            activeCycle.preSetControl = takeTail(controlEvents, 30u);
            if (!preStepTiming.has_value()) {
                preStepTiming = captureTiming("outside_cycle", "pc_before_execute", lastPc, lastPc);
            }
            preStepTiming->cycleName = "after_4500";
            preStepTiming->label = "pc_4500_before";
            recordTiming(*preStepTiming);
            auto post4500Timing = captureTiming("after_4500", "pc_4500_after", lastPc, cpu.PC);
            recordTiming(post4500Timing);
        }
        if (activeCycle.valid) {
            if (lastPc == 0x4090u && !activeCycle.reached4090) {
                activeCycle.reached4090 = true;
                activeCycle.first4090Step = currentStep;
                activeCycle.first4090CpuCycles = preInstructionCpuCycles;
                activeCycle.first4090Snapshot = postStepSnapshot;
                activeCycle.statsToFirst4090 = activeCycle.stats;
            }
            recordOpcodeStats(activeCycle.stats,
                              opcodeAtPc,
                              nextOpcodeByte,
                              thirdOpcodeByte,
                              fourthOpcodeByte,
                              cycles);
        }
        if (lastPc == 0x4B24u) {
            if (activeCycle.valid) {
                activeCycle.reached4B24 = true;
                if (c702Value == 0u) {
                    activeCycle.cleared = true;
                    activeCycle.clearStep = currentStep;
                    activeCycle.clearCpuCycles = totalCpuCycles;
                    activeCycle.clearSnapshot = postStepSnapshot;
                    auto clearTiming = captureTiming("after_4500", "pc_4b24_clear_after", lastPc, cpu.PC);
                    recordTiming(clearTiming);
                    collectBfTimingSinceLastClear = true;
                    bfTimingSinceLastClear.clear();
                    collectVdpWritesBetweenClearAndSet = true;
                    vdpWritesBetweenClearAndSet.clear();
                    collectBetweenSuccessfulClearAndFinalSetStats = true;
                    betweenSuccessfulClearAndFinalSetStats = OpcodeStats{};
                    lastSuccessfulCycle = activeCycle;
                    activeCycle = CycleTrace{};
                }
            }
        }
        if (isBranchOpcode(opcodeAtPc)) {
            uint16_t fallthrough = static_cast<uint16_t>(lastPc + 1u);
            uint16_t target = cpu.PC;
            bool conditional = false;
            bool taken = true;
            if (opcodeAtPc == 0x18u || isConditionalRelativeBranch(opcodeAtPc)) {
                fallthrough = static_cast<uint16_t>(lastPc + 2u);
                target = static_cast<uint16_t>(fallthrough + static_cast<int8_t>(nextOpcodeByte));
                conditional = opcodeAtPc != 0x18u;
                taken = cpu.PC == target;
            } else if (opcodeAtPc == 0xC3u || isConditionalAbsoluteControl(opcodeAtPc) || isCallOpcode(opcodeAtPc)) {
                fallthrough = static_cast<uint16_t>(lastPc + 3u);
                target = static_cast<uint16_t>(nextOpcodeByte | (static_cast<uint16_t>(thirdOpcodeByte) << 8u));
                conditional = opcodeAtPc != 0xC3u && opcodeAtPc != 0xCDu;
                taken = cpu.PC == target || (isCallOpcode(opcodeAtPc) && cpu.PC == target);
            } else if (opcodeAtPc == 0xE9u) {
                fallthrough = static_cast<uint16_t>(lastPc + 1u);
                target = cpu.PC;
                taken = true;
            } else if (isRetOpcode(opcodeAtPc)) {
                fallthrough = static_cast<uint16_t>(lastPc + 1u);
                target = cpu.PC;
                conditional = opcodeAtPc != 0xC9u && opcodeAtPc != 0xD9u;
                taken = cpu.PC != fallthrough;
            }
            const BranchEvent branch{
                currentStep,
                lastPc,
                target,
                fallthrough,
                opcodeAtPc,
                nextOpcodeByte,
                thirdOpcodeByte,
                conditional,
                taken,
                in4B24BranchNeighborhood(lastPc),
                postStepSnapshot,
            };
            if (inMegaManRegion(lastPc) || inMegaManRegion(target) || branch.inNeighborhoodBefore4B24) {
                pushTail(branchEvents, branch, tailLimit);
            }
            if (branch.inNeighborhoodBefore4B24) {
                pushTail(branchNeighborhoodEvents, branch, tailLimit);
            }
            if (activeCycle.valid) {
                pushCycleBranch(activeCycle, branch);
            }
        }
        if (isCallOpcode(opcodeAtPc)) {
            const uint16_t target = static_cast<uint16_t>(nextOpcodeByte | (static_cast<uint16_t>(thirdOpcodeByte) << 8u));
            const uint16_t fallthrough = static_cast<uint16_t>(lastPc + 3u);
            const bool taken = cpu.PC == target;
            if (taken && inMegaManRegion(target)) {
                const ControlEvent event{
                    currentStep,
                    "CALL",
                    lastPc,
                    target,
                    preStepSp,
                    cpu.SP,
                    postStepSnapshot,
                };
                pushTail(controlEvents, event, tailLimit);
                if (activeCycle.valid) {
                    pushCycleControl(activeCycle, event);
                }
            } else if (!taken && inMegaManRegion(fallthrough)) {
                const ControlEvent event{
                    currentStep,
                    "CALL_NOT_TAKEN",
                    lastPc,
                    target,
                    preStepSp,
                    cpu.SP,
                    postStepSnapshot,
                };
                pushTail(controlEvents, event, tailLimit);
                if (activeCycle.valid) {
                    pushCycleControl(activeCycle, event);
                }
            }
        } else if (isRetOpcode(opcodeAtPc)) {
            const uint16_t fallthrough = static_cast<uint16_t>(lastPc + 1u);
            const bool taken = cpu.PC != fallthrough;
            if (taken && (inMegaManRegion(lastPc) || inMegaManRegion(cpu.PC))) {
                const ControlEvent event{
                    currentStep,
                    "RET",
                    lastPc,
                    cpu.PC,
                    preStepSp,
                    cpu.SP,
                    postStepSnapshot,
                };
                pushTail(controlEvents, event, tailLimit);
                if (activeCycle.valid) {
                    pushCycleControl(activeCycle, event);
                }
            }
        }
        if (interruptServedThisStep) {
            inInterruptRoutine = true;
            lastInterruptStep = currentStep;
            const bool ie1 = (trackedVdpRegisters[0x00u] & 0x10u) != 0u;
            const bool ie0 = (trackedVdpRegisters[0x01u] & 0x20u) != 0u;
            pushTail(interruptEvents,
                     InterruptEvent{
                         currentStep,
                         lastPc,
                         cpu.PC,
                         cpu.SP,
                         interruptVectorThisStep,
                         cpu.interruptMode_,
                         vdp.currentScanline(),
                         c702BecameNonzero,
                         vdp.isIrqAsserted(),
                         vdp.isFrameInterruptPending(),
                         vdp.isLineInterruptPending(),
                         ie0,
                         ie1,
                     },
                     tailLimit);
            auto irqTiming = captureTiming(activeCycle.valid ? "after_4500" : "outside_cycle",
                                           "interrupt_entry_0038",
                                           lastPc,
                                           cpu.PC);
            recordTiming(irqTiming);
        }
        vdp.step(cycles);
        psg.step(cycles);
        if (vdp.takeIrqAsserted()) {
            interruptRequested = true;
            auto latchTiming = captureTiming(activeCycle.valid ? "after_4500" : "outside_cycle",
                                             "machine_irq_latched_after_vdp_step",
                                             lastPc,
                                             cpu.PC);
            recordTiming(latchTiming);
        }
        if (inInterruptRoutine &&
            (opcodeAtPc == 0xC9u || (opcodeAtPc == 0xEDu && (nextOpcodeByte == 0x45u || nextOpcodeByte == 0x4Du)))) {
            inInterruptRoutine = false;
        }
        if (cpu.PC == lastPc) {
            ++samePcRun;
        } else {
            samePcRun = 0;
        }
        if (stopOnC702SpinSamePcCount.has_value()) {
            if (samePcRun >= *stopOnC702SpinSamePcCount &&
                (cpu.PC == 0x414Fu || cpu.PC == 0x4152u) &&
                c702Value == 0xFFu) {
                std::cerr << "stop-on-c702-spin triggered at step=" << currentStep
                          << " pc=" << hex16(cpu.PC) << '\n';
                break;
            }
        }
    }

    std::vector<std::pair<uint16_t, uint64_t>> hotPcs(pcCounts.begin(), pcCounts.end());
    std::sort(hotPcs.begin(), hotPcs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second > rhs.second;
    });
    if (activeCycle.valid) {
        finalCycle = activeCycle;
    }

    auto printSnapshot = [&](const CpuSnapshot& snapshot) {
        std::cout << "step=" << snapshot.step
                  << " pc=" << hex16(snapshot.pc)
                  << " af=" << hex16(snapshot.af)
                  << " bc=" << hex16(snapshot.bc)
                  << " de=" << hex16(snapshot.de)
                  << " hl=" << hex16(snapshot.hl)
                  << " ix=" << hex16(snapshot.ix)
                  << " iy=" << hex16(snapshot.iy)
                  << " sp=" << hex16(snapshot.sp);
        printBankState(snapshot.banks);
        std::cout << " c702_before=" << hex8(snapshot.c702Before)
                  << " c702_after=" << hex8(snapshot.c702After)
                  << " opcodes";
        printOpcodeWindow(snapshot.opcodes);
    };
    auto printBranch = [&](const BranchEvent& branch) {
        std::cout << "step=" << branch.step
                  << " pc=" << hex16(branch.pc)
                  << " opcode=" << hex8(branch.opcode)
                  << " operands=" << hex8(branch.operand0) << ',' << hex8(branch.operand1)
                  << " target=" << hex16(branch.target)
                  << " fallthrough=" << hex16(branch.fallthrough)
                  << " conditional=" << (branch.conditional ? "yes" : "no")
                  << " taken=" << (branch.taken ? "yes" : "no")
                  << " near_4b24=" << (branch.inNeighborhoodBefore4B24 ? "yes" : "no")
                  << " regs af=" << hex16(branch.snapshot.af)
                  << " bc=" << hex16(branch.snapshot.bc)
                  << " de=" << hex16(branch.snapshot.de)
                  << " hl=" << hex16(branch.snapshot.hl)
                  << " ix=" << hex16(branch.snapshot.ix)
                  << " iy=" << hex16(branch.snapshot.iy)
                  << " sp=" << hex16(branch.snapshot.sp)
                  << " c702_before=" << hex8(branch.snapshot.c702Before)
                  << " c702_after=" << hex8(branch.snapshot.c702After);
        printBankState(branch.snapshot.banks);
    };
    auto printControl = [&](const ControlEvent& event) {
        std::cout << "step=" << event.step
                  << " kind=" << event.kind
                  << " pc=" << hex16(event.pc)
                  << " target=" << hex16(event.target)
                  << " sp_before=" << hex16(event.spBefore)
                  << " sp_after=" << hex16(event.spAfter)
                  << " regs af=" << hex16(event.snapshot.af)
                  << " bc=" << hex16(event.snapshot.bc)
                  << " de=" << hex16(event.snapshot.de)
                  << " hl=" << hex16(event.snapshot.hl)
                  << " ix=" << hex16(event.snapshot.ix)
                  << " iy=" << hex16(event.snapshot.iy)
                  << " c702_before=" << hex8(event.snapshot.c702Before)
                  << " c702_after=" << hex8(event.snapshot.c702After);
        printBankState(event.snapshot.banks);
    };
    auto printFlagSetter = [&](const FlagSetterEvent& event) {
        std::cout << "step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " opcode=" << hex8(event.opcode)
                  << " operands=" << hex8(event.operand0) << ',' << hex8(event.operand1) << ',' << hex8(event.operand2)
                  << " decode=\"" << decodeBrief(event.opcode, event.operand0, event.operand1, event.operand2) << '"'
                  << " af_before=" << hex16(event.afBefore)
                  << " flags_before={" << flagsSummary(static_cast<uint8_t>(event.afBefore & 0x00FFu)) << '}'
                  << " af_after=" << hex16(event.afAfter)
                  << " flags_after={" << flagsSummary(static_cast<uint8_t>(event.afAfter & 0x00FFu)) << '}';
    };
    auto printBranch45F1 = [&](const Branch45F1Event& event) {
        std::cout << "step=" << event.step
                  << " pc=0x45F1"
                  << " opcode=" << hex8(event.opcode)
                  << " operand=" << hex8(event.operand)
                  << " target=" << hex16(event.target)
                  << " fallthrough=" << hex16(event.fallthrough)
                  << " taken=" << (event.taken ? "yes" : "no")
                  << " a=" << hex8(static_cast<uint8_t>(event.af >> 8u))
                  << " f=" << hex8(static_cast<uint8_t>(event.af & 0x00FFu))
                  << " flags={" << flagsSummary(static_cast<uint8_t>(event.af & 0x00FFu)) << '}'
                  << " bc=" << hex16(event.bc)
                  << " de=" << hex16(event.de)
                  << " hl=" << hex16(event.hl)
                  << " ix=" << hex16(event.ix)
                  << " iy=" << hex16(event.iy)
                  << " sp=" << hex16(event.sp)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " c148=" << hex8(event.c148)
                  << " c149=" << hex8(event.c149)
                  << " c14a=" << hex8(event.c14a)
                  << " c14b=" << hex8(event.c14b)
                  << " c14e=" << hex8(event.c14e)
                  << " c736=" << hex8(event.c736)
                  << " c737=" << hex8(event.c737)
                  << " c73c=" << hex8(event.c73c)
                  << " c73d=" << hex8(event.c73d)
                  << " c7b7=" << hex16(event.c7b7)
                  << " c7b9=" << hex16(event.c7b9)
                  << " c7cd=" << hex16(event.c7cd)
                  << " c7cf=" << hex16(event.c7cf);
        printBankState(event.banks);
        std::cout << " opcodes";
        printOpcodeWindow(event.opcodes);
        std::cout << " flag_setter ";
        printFlagSetter(event.flagSetter);
    };
    auto printMapperWrite = [&](const MapperWriteEvent& event) {
        std::cout << "step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " addr=" << hex16(event.address)
                  << " prev_mirror=" << hex8(event.previousValue)
                  << " value=" << hex8(event.value)
                  << " c148=" << hex8(event.c148)
                  << " c14a=" << hex8(event.c14a)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " before";
        printBankState(event.before);
        std::cout << " after";
        printBankState(event.after);
        std::cout << " opcodes";
        printOpcodeWindow(event.opcodes);
    };
    auto instructionLength = [](uint8_t opcode, uint8_t operand0) {
        if (opcode == 0xCBu || opcode == 0xEDu) return 2u;
        if (opcode == 0xDDu || opcode == 0xFDu) return operand0 == 0xCBu ? 4u : 2u;
        switch (opcode) {
        case 0x01u: case 0x11u: case 0x21u: case 0x22u: case 0x2Au: case 0x31u:
        case 0x32u: case 0x3Au: case 0xC2u: case 0xC3u: case 0xC4u: case 0xCAu:
        case 0xCCu: case 0xCDu: case 0xD2u: case 0xD4u: case 0xDAu: case 0xDCu:
        case 0xE2u: case 0xE4u: case 0xEAu: case 0xECu: case 0xF2u: case 0xF4u:
        case 0xFAu: case 0xFCu:
            return 3u;
        case 0x06u: case 0x0Eu: case 0x10u: case 0x16u: case 0x18u: case 0x1Eu:
        case 0x20u: case 0x26u: case 0x28u: case 0x2Eu: case 0x30u: case 0x36u:
        case 0x38u: case 0x3Eu: case 0xC6u: case 0xCEu: case 0xD3u: case 0xD6u:
        case 0xDBu: case 0xDEu: case 0xE6u: case 0xEEu: case 0xF6u: case 0xFEu:
            return 2u;
        default:
            return 1u;
        }
    };
    auto printDecodedBankContext = [&](uint8_t bank, uint16_t start, uint16_t end) {
        std::cout << "decoded_45d0_4610 bank=" << hex8(bank) << '\n';
        uint16_t pc = start;
        while (pc <= end) {
            const std::size_t offset = static_cast<std::size_t>(bank) * 0x4000u + static_cast<std::size_t>(pc & 0x3FFFu);
            const uint8_t b0 = offset < rom.size() ? rom[offset] : 0xFFu;
            const uint8_t b1 = offset + 1u < rom.size() ? rom[offset + 1u] : 0xFFu;
            const uint8_t b2 = offset + 2u < rom.size() ? rom[offset + 2u] : 0xFFu;
            const uint8_t b3 = offset + 3u < rom.size() ? rom[offset + 3u] : 0xFFu;
            const auto len = instructionLength(b0, b1);
            std::cout << "  " << hex16(pc) << " bank=" << hex8(bank)
                      << " rom_offset=0x" << std::hex << std::uppercase << offset << std::dec
                      << " bytes=" << hex8(b0);
            if (len > 1u) std::cout << ' ' << hex8(b1);
            if (len > 2u) std::cout << ' ' << hex8(b2);
            if (len > 3u) std::cout << ' ' << hex8(b3);
            std::cout << "  " << decodeBrief(b0, b1, b2, b3) << '\n';
            pc = static_cast<uint16_t>(pc + len);
        }
    };
    auto printTiming = [&](const TimingSnapshot& timing) {
        std::cout << "step=" << timing.step
                  << " cpu_cycles=" << timing.cpuCycles
                  << " cycle=" << timing.cycleName
                  << " label=" << timing.label
                  << " pc=" << hex16(timing.pc)
                  << " pc_after=" << hex16(timing.pcAfter);
        if (timing.hasStatusValue) {
            std::cout << " status=" << hex8(timing.statusValue);
        }
        std::cout << " scanline=" << static_cast<int>(timing.scanline)
                  << " vdp_dot=";
        if (timing.hasVdpDot) {
            std::cout << timing.vdpDot;
        } else {
            std::cout << "n/a";
        }
        std::cout << " vcounter=" << hex8(timing.vCounter)
                  << " hcounter=" << hex8(timing.hCounter)
                  << " frame_irq_pending=" << (timing.framePending ? "yes" : "no")
                  << " line_irq_pending=" << (timing.linePending ? "yes" : "no")
                  << " irq_asserted=" << (timing.irqAsserted ? "yes" : "no")
                  << " machine_irq_pending=" << (timing.machineIrqPending ? "yes" : "no")
                  << " reg0=" << hex8(timing.reg0)
                  << " reg1=" << hex8(timing.reg1)
                  << " reg10=" << hex8(timing.reg10)
                  << " im=" << static_cast<int>(timing.im)
                  << " ime=" << (timing.ime ? "yes" : "no")
                  << " iff1=" << (timing.iff1 ? "yes" : "no")
                  << " iff2=" << (timing.iff2 ? "yes" : "no");
    };
    auto sortedByCount = [](const std::map<std::string, uint64_t>& counts) {
        std::vector<std::pair<std::string, uint64_t>> entries(counts.begin(), counts.end());
        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });
        return entries;
    };
    auto printOpcodeStats = [&](const char* label, const OpcodeStats& stats, std::size_t limit) {
        std::cout << label
                  << " instructions=" << stats.instructions
                  << " cycles=" << stats.cycles
                  << '\n';
        std::cout << "  groups";
        for (const auto& group : {"base", "CB", "ED", "DD", "FD", "DDCB", "FDCB"}) {
            const auto countIt = stats.groupCounts.find(group);
            const auto cycleIt = stats.groupCycles.find(group);
            const auto count = countIt == stats.groupCounts.end() ? 0u : countIt->second;
            const auto cycles = cycleIt == stats.groupCycles.end() ? 0u : cycleIt->second;
            std::cout << ' ' << group << "_count=" << count << ' ' << group << "_cycles=" << cycles;
        }
        std::cout << '\n';
        std::cout << "  top_opcodes\n";
        const auto entries = sortedByCount(stats.opcodeCounts);
        for (std::size_t i = 0; i < std::min(limit, entries.size()); ++i) {
            const auto cyclesIt = stats.opcodeCycles.find(entries[i].first);
            const auto cycles = cyclesIt == stats.opcodeCycles.end() ? 0u : cyclesIt->second;
            std::cout << "    opcode=" << entries[i].first
                      << " count=" << entries[i].second
                      << " cycles=" << cycles
                      << '\n';
        }
    };
    auto printCycle = [&](const char* label, const CycleTrace& cycle) {
        std::cout << label
                  << " valid=" << (cycle.valid ? "yes" : "no")
                  << " reached_4b24=" << (cycle.reached4B24 ? "yes" : "no")
                  << " cleared=" << (cycle.cleared ? "yes" : "no")
                  << " reached_4090=" << (cycle.reached4090 ? "yes" : "no")
                  << " set_step=" << (cycle.valid ? std::to_string(cycle.setStep) : std::string("n/a"))
                  << " clear_step=" << (cycle.reached4B24 ? std::to_string(cycle.clearStep) : std::string("n/a"))
                  << " first_4090_step=" << (cycle.reached4090 ? std::to_string(cycle.first4090Step) : std::string("n/a"))
                  << " set_cpu_cycles=" << (cycle.valid ? std::to_string(cycle.setCpuCycles) : std::string("n/a"))
                  << " clear_cpu_cycles=" << (cycle.reached4B24 ? std::to_string(cycle.clearCpuCycles) : std::string("n/a"))
                  << " first_4090_cpu_cycles="
                  << (cycle.reached4090 ? std::to_string(cycle.first4090CpuCycles) : std::string("n/a"))
                  << '\n';
        if (cycle.valid) {
            std::cout << "  set_snapshot ";
            printSnapshot(cycle.setSnapshot);
            std::cout << '\n';
        }
        if (cycle.reached4B24) {
            std::cout << "  clear_snapshot ";
            printSnapshot(cycle.clearSnapshot);
            std::cout << '\n';
        }
        if (cycle.reached4090) {
            std::cout << "  first_4090_snapshot ";
            printSnapshot(cycle.first4090Snapshot);
            std::cout << '\n';
            std::cout << "  opcode_stats_to_first_4090\n";
            printOpcodeStats("    cycle_to_first_4090_opcode_histogram", cycle.statsToFirst4090, 20u);
        }
        std::cout << "  opcode_stats\n";
        printOpcodeStats("    cycle_opcode_histogram", cycle.stats, 20u);
        std::cout << "  irq_timing_milestones\n";
        for (const auto& timing : cycle.timingMilestones) {
            std::cout << "    ";
            printTiming(timing);
            std::cout << '\n';
        }
        std::cout << "  irq_timing\n";
        for (const auto& timing : cycle.timing) {
            std::cout << "    ";
            printTiming(timing);
            std::cout << '\n';
        }
        std::cout << "  pre_set_control_tail\n";
        for (const auto& event : cycle.preSetControl) {
            std::cout << "    ";
            printControl(event);
            std::cout << '\n';
        }
        std::cout << "  pre_set_branch_tail\n";
        for (const auto& event : cycle.preSetBranches) {
            std::cout << "    ";
            printBranch(event);
            std::cout << '\n';
        }
        std::cout << "  control_head\n";
        for (const auto& event : cycle.controlHead) {
            std::cout << "    ";
            printControl(event);
            std::cout << '\n';
        }
        std::cout << "  control_tail\n";
        for (const auto& event : cycle.control) {
            std::cout << "    ";
            printControl(event);
            std::cout << '\n';
        }
        std::cout << "  branch_head\n";
        for (const auto& event : cycle.branchHead) {
            std::cout << "    ";
            printBranch(event);
            std::cout << '\n';
        }
        std::cout << "  branch_tail\n";
        for (const auto& event : cycle.branches) {
            std::cout << "    ";
            printBranch(event);
            std::cout << '\n';
        }
    };

    std::cout << "steps=" << currentStep << " pc=" << hex16(cpu.PC)
              << " sp=" << hex16(cpu.SP) << " af=" << hex16(cpu.AF)
              << " bc=" << hex16(cpu.BC) << " de=" << hex16(cpu.DE)
              << " hl=" << hex16(cpu.HL) << " scanline=" << static_cast<int>(vdp.currentScanline())
              << '\n';
    std::cout << "halted=" << (cpu.halted_ ? "yes" : "no")
              << " ime=" << (cpu.IME ? "yes" : "no")
              << " iff1=" << (cpu.IFF1 ? "yes" : "no")
              << " iff2=" << (cpu.IFF2 ? "yes" : "no")
              << " im=" << static_cast<int>(cpu.interruptMode_)
              << " machine_irq_pending=" << (interruptRequested ? "yes" : "no")
              << " vdp_irq_asserted=" << (vdp.isIrqAsserted() ? "yes" : "no")
              << " frame_irq_pending=" << (vdp.isFrameInterruptPending() ? "yes" : "no")
              << " line_irq_pending=" << (vdp.isLineInterruptPending() ? "yes" : "no")
              << " irq_provider_calls=" << interruptProviderCalls
              << " irq_provider_served=" << interruptProviderServed
              << '\n';

    const bool trackedIe1 = (trackedVdpRegisters[0x00u] & 0x10u) != 0u;
    const bool trackedIe0 = (trackedVdpRegisters[0x01u] & 0x20u) != 0u;
    std::cout << "vdp_irq_state"
              << " reg0=" << hex8(trackedVdpRegisters[0x00u])
              << " ie1_line=" << (trackedIe1 ? "enabled" : "disabled")
              << " reg1=" << hex8(trackedVdpRegisters[0x01u])
              << " ie0_frame=" << (trackedIe0 ? "enabled" : "disabled")
              << " reg10_line_reload=" << hex8(trackedVdpRegisters[0x0Au])
              << " frame_irq_pending=" << (vdp.isFrameInterruptPending() ? "yes" : "no")
              << " line_irq_pending=" << (vdp.isLineInterruptPending() ? "yes" : "no")
              << " vdp_irq_asserted=" << (vdp.isIrqAsserted() ? "yes" : "no")
              << " line_pending_without_assertion="
              << (vdp.isLineInterruptPending() && !vdp.isIrqAsserted()
                      ? (trackedIe1 ? "suspicious_ie1_enabled" : "expected_ie1_disabled")
                      : "no")
              << '\n';

    printDecodedBankContext(0x19u, 0x45D0u, 0x4610u);
    printDecodedBankContext(0x12u, 0x45D0u, 0x4610u);

    std::cout << "branch_45f1_events count=" << branch45F1Count
              << " tail_count=" << branch45F1Events.size()
              << " full_trace=/tmp/pc45f1_trace.log\n";
    for (const auto& event : branch45F1Events) {
        std::cout << "  ";
        printBranch45F1(event);
        std::cout << '\n';
    }
    if (branch45F1Count != 0u) {
        std::cout << "branch_45f1_success_vs_final\n";
        if (branch45F1FirstSuccess.has_value()) {
            std::cout << "  success ";
            printBranch45F1(*branch45F1FirstSuccess);
            std::cout << '\n';
        } else {
            std::cout << "  success n/a\n";
        }
        if (branch45F1FinalDivergent.has_value()) {
            std::cout << "  final ";
            printBranch45F1(*branch45F1FinalDivergent);
            std::cout << '\n';
        } else {
            std::cout << "  final n/a\n";
        }
    }

    std::cout << "mapper_writes_recent count=" << mapperWrites.size() << '\n';
    for (const auto& event : mapperWrites) {
        std::cout << "  ";
        printMapperWrite(event);
        std::cout << '\n';
    }
    std::cout << "mapper_writes_last_c14a_clear_to_final_set"
              << " clear_step="
              << (lastC14AClearStep.has_value() ? std::to_string(*lastC14AClearStep) : std::string("n/a"))
              << " final_set_step="
              << (finalC14ASetStep.has_value() ? std::to_string(*finalC14ASetStep) : std::string("n/a"))
              << " count=" << mapperWritesLastC14AClearToFinalSet.size() << '\n';
    for (const auto& event : mapperWritesLastC14AClearToFinalSet) {
        std::cout << "  ";
        printMapperWrite(event);
        std::cout << '\n';
    }
    std::cout << "range_45d0_45f1_trace full_trace=/tmp/pc45d0_45f1_trace.log\n";
    std::cout << "pointer_ram_writes count=" << pointerWrites.size()
              << " full_trace=/tmp/pointer_ram_writes.log\n";
    for (const auto& event : pointerWrites) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " addr=" << hex16(event.address)
                  << " prev=" << hex8(event.previousValue)
                  << " value=" << hex8(event.value)
                  << " af=" << hex16(event.af)
                  << " bc=" << hex16(event.bc)
                  << " de=" << hex16(event.de)
                  << " hl=" << hex16(event.hl)
                  << " ix=" << hex16(event.ix)
                  << " iy=" << hex16(event.iy)
                  << " sp=" << hex16(event.sp)
                  << " scanline=" << static_cast<int>(event.scanline);
        printBankState(event.banks);
        std::cout << '\n';
    }

    std::cout << "opcode_window";
    for (int offset = -8; offset <= 8; ++offset) {
        const auto address = static_cast<uint16_t>(cpu.PC + offset);
        std::cout << ' ' << hex16(address) << ':' << hex8(mem.read(address));
    }
    std::cout << '\n';

    std::cout << "hot_pcs";
    for (std::size_t i = 0; i < std::min<std::size_t>(hotPcs.size(), 12u); ++i) {
        std::cout << ' ' << hex16(hotPcs[i].first) << '=' << hotPcs[i].second;
    }
    std::cout << " same_pc_run=" << samePcRun << '\n';

    std::cout << "recent_pcs";
    for (std::size_t i = 0; i < recentPcs.size(); ++i) {
        const auto idx = (recentPcIndex + i) % recentPcs.size();
        std::cout << ' ' << hex16(recentPcs[idx]);
    }
    std::cout << '\n';

    std::cout << "watched_ports\n";
    for (const uint8_t port : {0x00u, 0xDCu, 0xDDu, 0x7Eu, 0x7Fu, 0xBEu, 0xBFu}) {
        std::cout << "  port=" << hex8(port)
                  << " reads=" << readCounts[port]
                  << " last_read=" << (lastRead.contains(port) ? hex8(lastRead[port]) : std::string("n/a"))
                  << " writes=" << writeCounts[port]
                  << " last_write=" << (lastWrite.contains(port) ? hex8(lastWrite[port]) : std::string("n/a"))
                  << '\n';
    }

    std::cout << "recent_io\n";
    for (const auto& event : events) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << ' ' << event.kind
                  << " port=" << hex8(event.port)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " after_c702_nonzero=" << (event.afterC702Nonzero ? "yes" : "no")
                  << " in_isr=" << (event.inInterruptRoutine ? "yes" : "no")
                  << '\n';
    }

    std::cout << "watched_ram";
    for (const uint16_t address : {0xC14Au, 0xC14Bu, 0xC14Cu, 0xC18Eu, 0xC190u, 0xC702u}) {
        std::cout << ' ' << hex16(address) << '=' << hex8(mem.read(address));
    }
    std::cout << '\n';

    std::cout << "recent_ram_writes\n";
    for (const auto& event : memEvents) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " addr=" << hex16(event.address)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " opcodes";
        printOpcodeWindow(event.opcodes);
        std::cout << '\n';
    }

    std::cout << "c702_summary"
              << " current=" << hex8(mem.read(0xC702u))
              << " tracked_current=" << hex8(c702Value)
              << " ever_written=" << (c702EverWritten ? "yes" : "no")
              << " ever_became_nonzero=" << (c702EverBecameNonzero ? "yes" : "no")
              << " first_nonzero_step="
              << (firstC702NonzeroStep.has_value() ? std::to_string(*firstC702NonzeroStep) : std::string("n/a"))
              << " cleared_after_nonzero=" << (c702ClearedAfterNonzero ? "yes" : "no")
              << " first_clear_after_nonzero_step="
              << (firstC702ClearAfterNonzeroStep.has_value()
                      ? std::to_string(*firstC702ClearAfterNonzeroStep)
                      : std::string("n/a"))
              << " repeated_nonzero_writes=" << (c702RepeatedNonzeroWrite ? "yes" : "no")
              << " repeated_zero_writes=" << (c702RepeatedZeroWrite ? "yes" : "no");
    if (c702EverBecameNonzero && !c702ClearedAfterNonzero) {
        std::cout << " classification=stuck_high_or_never_cleared_in_window";
    } else if (c702ClearedAfterNonzero && mem.read(0xC702u) != 0u) {
        std::cout << " classification=cleared_then_set_again";
    } else if (c702ClearedAfterNonzero) {
        std::cout << " classification=cleared_after_nonzero";
    } else if (c702RepeatedNonzeroWrite) {
        std::cout << " classification=repeated_nonzero_without_clear";
    } else {
        std::cout << " classification=no_nonzero_flag_observed";
    }
    std::cout << '\n';

    std::cout << "c702_writes\n";
    for (const auto& event : c702Writes) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " previous=" << hex8(event.previousValue)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " opcodes";
        printOpcodeWindow(event.opcodes);
        std::cout << '\n';
    }

    std::cout << "c702_reads\n";
    for (const auto& event : c702Reads) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << '\n';
    }

    // --- Full write histories for addresses of interest ---
    auto printRamHistory = [&](uint16_t address) {
        std::cout << "full_write_history addr=" << hex16(address) << '\n';
        // From ramWrites
        for (const auto& w : ramWrites) {
            if (w.address == address) {
                std::cout << "  step=" << w.step << " pc=" << hex16(w.pc)
                          << " prev=" << hex8(w.previousValue) << " written=" << hex8(w.value)
                          << " scanline=" << static_cast<int>(w.scanline)
                          << " regs af=" << hex16(w.af) << " bc=" << hex16(w.bc)
                          << " de=" << hex16(w.de) << " hl=" << hex16(w.hl)
                          << " ix=" << hex16(w.ix) << " iy=" << hex16(w.iy)
                          << " sp=" << hex16(w.sp)
                          << " opcodes";
                printOpcodeWindow(w.opcodes);
                std::cout << '\n';
            }
        }
        // Also surface any dedicated C14A write captures (full metadata)
        if (address == 0xC14Au) {
            for (const auto& w : c14aWrites) {
                std::cout << "  c14a_step=" << w.step << " pc=" << hex16(w.pc)
                          << " prev=" << hex8(w.previousValue) << " written=" << hex8(w.value)
                          << " scanline=" << static_cast<int>(w.scanline)
                          << " regs af=" << hex16(w.af) << " bc=" << hex16(w.bc)
                          << " de=" << hex16(w.de) << " hl=" << hex16(w.hl)
                          << " ix=" << hex16(w.ix) << " iy=" << hex16(w.iy)
                          << " sp=" << hex16(w.sp)
                          << " machine_irq_pending=" << (w.machine_irq_pending ? "yes" : "no")
                          << " vdp_frame_pending=" << (w.vdp_frame_pending ? "yes" : "no")
                          << " vdp_line_pending=" << (w.vdp_line_pending ? "yes" : "no")
                          << " opcodes";
                printOpcodeWindow(w.opcodes);
                std::cout << std::endl;
            }
        }
        // For C700..C710, also print dedicated ring history
        if (address >= 0xC700u && address <= 0xC710u) {
            const std::size_t idx = static_cast<std::size_t>(address - 0xC700u);
            std::cout << "  c700_ring_history (last " << c700History[idx].size() << ")\n";
            for (const auto& w : c700History[idx]) {
                std::cout << "    step=" << w.step << " pc=" << hex16(w.pc)
                          << " prev=" << hex8(w.previousValue) << " written=" << hex8(w.value)
                          << " scanline=" << static_cast<int>(w.scanline)
                          << " regs af=" << hex16(w.af) << " bc=" << hex16(w.bc)
                          << " de=" << hex16(w.de) << " hl=" << hex16(w.hl)
                          << " ix=" << hex16(w.ix) << " iy=" << hex16(w.iy)
                          << " sp=" << hex16(w.sp)
                          << " opcodes";
                printOpcodeWindow(w.opcodes);
                std::cout << std::endl;
            }
        }
    };

    // Print histories for requested addresses
    for (const uint16_t addr : {0xC14Au, 0xC702u, 0xC703u, 0xC503u, 0xC603u}) {
        printRamHistory(addr);
    }

    // Print opcode window and decode around PC=0x4B7B
    {
        const uint16_t probePc = 0x4B7Bu;
        const auto window = captureOpcodeWindow(mem, probePc);
        std::cout << "opcode_window_4B7B";
        printOpcodeWindow(window);
        std::cout << '\n';
        analyzeOpcodeWindow(window);
    }

    // Print snapshot byte ranges at last successful set and final stuck set
    auto printSnapshotRange = [&](const std::string& tag, const CpuSnapshot& snap, uint16_t startAddr, uint16_t endAddr) {
        std::cout << tag << " " << hex16(startAddr) << "-" << hex16(endAddr) << '\n';
        const int base = 0xC4E0;
        for (uint16_t a = startAddr; a <= endAddr; ++a) {
            const int idx = static_cast<int>(a) - base;
            if (idx >= 0 && static_cast<std::size_t>(idx) < snap.ram_c4e0_c720.size()) {
                std::cout << ' ' << hex16(a) << ':' << hex8(snap.ram_c4e0_c720[static_cast<std::size_t>(idx)]);
            } else {
                std::cout << ' ' << hex16(a) << ":n/a";
            }
        }
        std::cout << '\n';
    };

    if (lastSuccessfulCycle.valid) {
        printSnapshotRange("last_successful_set_snapshot", lastSuccessfulCycle.setSnapshot, 0xC4F8u, 0xC508u);
        printSnapshotRange("last_successful_set_snapshot", lastSuccessfulCycle.setSnapshot, 0xC5F8u, 0xC608u);
        printSnapshotRange("last_successful_set_snapshot", lastSuccessfulCycle.setSnapshot, 0xC6F8u, 0xC708u);
    }
    if (finalCycle.valid) {
        printSnapshotRange("final_set_snapshot", finalCycle.setSnapshot, 0xC4F8u, 0xC508u);
        printSnapshotRange("final_set_snapshot", finalCycle.setSnapshot, 0xC5F8u, 0xC608u);
        printSnapshotRange("final_set_snapshot", finalCycle.setSnapshot, 0xC6F8u, 0xC708u);
    }

    std::cout << "interrupt_events\n";
    for (const auto& event : interruptEvents) {
        std::cout << "  step=" << event.step
                  << " pc_before=" << hex16(event.pcBefore)
                  << " pc_after=" << hex16(event.pcAfter)
                  << " sp_after=" << hex16(event.spAfter)
                  << " vector=" << hex8(event.vector)
                  << " im=" << static_cast<int>(event.mode)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " after_c702_nonzero=" << (event.afterC702Nonzero ? "yes" : "no")
                  << " vdp_irq_asserted=" << (event.vdpIrqAsserted ? "yes" : "no")
                  << " frame_pending=" << (event.framePending ? "yes" : "no")
                  << " line_pending=" << (event.linePending ? "yes" : "no")
                  << " ie0_frame=" << (event.ie0 ? "enabled" : "disabled")
                  << " ie1_line=" << (event.ie1 ? "enabled" : "disabled")
                  << '\n';
    }

    std::cout << "bf_status_reads\n";
    for (const auto& event : bfStatusReads) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " after_c702_nonzero=" << (event.afterC702Nonzero ? "yes" : "no")
                  << " in_isr=" << (event.inInterruptRoutine ? "yes" : "no")
                  << '\n';
    }
    std::cout << "bf_status_summary"
              << " first_after_c702_nonzero=";
    if (firstBfStatusReadAfterC702Nonzero.has_value()) {
        const auto& event = *firstBfStatusReadAfterC702Nonzero;
        std::cout << "step=" << event.step << ",pc=" << hex16(event.pc)
                  << ",value=" << hex8(event.value)
                  << ",in_isr=" << (event.inInterruptRoutine ? "yes" : "no");
    } else {
        std::cout << "n/a";
    }
    std::cout << " first_after_interrupt_after_c702_nonzero=";
    if (firstBfStatusReadAfterInterruptAfterC702Nonzero.has_value()) {
        const auto& event = *firstBfStatusReadAfterInterruptAfterC702Nonzero;
        std::cout << "step=" << event.step << ",pc=" << hex16(event.pc)
                  << ",value=" << hex8(event.value)
                  << ",in_isr=" << (event.inInterruptRoutine ? "yes" : "no");
    } else {
        std::cout << "n/a";
    }
    std::cout << '\n';

    std::cout << "vdp_register_writes\n";
    for (const auto& event : vdpRegisterWrites) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " reg=" << static_cast<int>(event.reg)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " ie0_frame=" << (event.ie0 ? "enabled" : "disabled")
                  << " ie1_line=" << (event.ie1 ? "enabled" : "disabled")
                  << " line_reload=" << hex8(event.lineReload)
                  << '\n';
    }

    std::cout << "vdp_register_writes_between_success_clear_and_final_set\n";
    for (const auto& event : vdpWritesBetweenClearAndSet) {
        std::cout << "  step=" << event.step
                  << " pc=" << hex16(event.pc)
                  << " reg=" << static_cast<int>(event.reg)
                  << " value=" << hex8(event.value)
                  << " scanline=" << static_cast<int>(event.scanline)
                  << " ie0_frame=" << (event.ie0 ? "enabled" : "disabled")
                  << " ie1_line=" << (event.ie1 ? "enabled" : "disabled")
                  << " line_reload=" << hex8(event.lineReload)
                  << '\n';
    }

    std::cout << "irq_timing_tail\n";
    for (const auto& timing : timingSnapshots) {
        std::cout << "  ";
        printTiming(timing);
        std::cout << '\n';
    }

    std::cout << "bf_status_reads_since_last_successful_clear\n";
    for (const auto& timing : bfTimingSinceLastClear) {
        std::cout << "  ";
        printTiming(timing);
        std::cout << '\n';
    }

    std::cout << "trace_pc_counts";
    for (const uint16_t pc : {0x44D0u, 0x4500u, 0x4B24u, 0x414Fu, 0x4152u}) {
        std::cout << ' ' << hex16(pc) << '=' << tracePcCounts[pc];
    }
    std::cout << '\n';

    std::cout << "trace_pc_hits\n";
    for (const auto& hit : tracePcHits) {
        std::cout << "  ";
        printSnapshot(hit);
        std::cout << '\n';
    }

    std::cout << "control_events\n";
    for (const auto& event : controlEvents) {
        std::cout << "  ";
        printControl(event);
        std::cout << '\n';
    }

    std::cout << "branch_neighborhood_before_4b24\n";
    for (const auto& event : branchNeighborhoodEvents) {
        std::cout << "  ";
        printBranch(event);
        std::cout << '\n';
    }

    std::cout << "branch_events_tail\n";
    for (const auto& event : branchEvents) {
        std::cout << "  ";
        printBranch(event);
        std::cout << '\n';
    }

    std::cout << "cycle_comparison_summary"
              << " last_success_valid=" << (lastSuccessfulCycle.valid ? "yes" : "no")
              << " final_valid=" << (finalCycle.valid ? "yes" : "no")
              << " final_4500_step=" << (finalCycle.valid ? std::to_string(finalCycle.setStep) : std::string("n/a"))
              << " reached_4b24_after_final_4500="
              << (finalCycle.valid && finalCycle.reached4B24 ? "yes" : "no")
              << '\n';
    std::cout << "cycle_delta_comparison\n";
    if (lastSuccessfulCycle.valid && lastSuccessfulCycle.reached4B24) {
        std::cout << "  successful_set_to_clear"
                  << " instructions=" << lastSuccessfulCycle.stats.instructions
                  << " cycles=" << (lastSuccessfulCycle.clearCpuCycles - lastSuccessfulCycle.setCpuCycles)
                  << " steps=" << (lastSuccessfulCycle.clearStep - lastSuccessfulCycle.setStep)
                  << '\n';
    } else {
        std::cout << "  successful_set_to_clear n/a\n";
    }
    if (finalCycle.valid && finalCycle.reached4090) {
        std::cout << "  final_set_to_first_4090"
                  << " instructions=" << finalCycle.statsToFirst4090.instructions
                  << " cycles=" << (finalCycle.first4090CpuCycles - finalCycle.setCpuCycles)
                  << " steps=" << (finalCycle.first4090Step - finalCycle.setStep)
                  << '\n';
    } else {
        std::cout << "  final_set_to_first_4090 n/a\n";
    }
    std::cout << "  successful_clear_to_final_set"
              << " instructions=" << betweenSuccessfulClearAndFinalSetStats.instructions
              << " cycles=" << betweenSuccessfulClearAndFinalSetStats.cycles
              << '\n';
    printOpcodeStats("successful_clear_to_final_set_opcode_histogram",
                     betweenSuccessfulClearAndFinalSetStats,
                     30u);
    printCycle("last_successful_set_clear_cycle", lastSuccessfulCycle);
    printCycle("final_set_stuck_cycle", finalCycle);

    std::cout << "irq_timing_comparison\n";
    std::cout << "  successful_cycle\n";
    for (const auto& timing : lastSuccessfulCycle.timingMilestones) {
        std::cout << "    ";
        printTiming(timing);
        std::cout << '\n';
    }
    std::cout << "  final_cycle\n";
    for (const auto& timing : finalCycle.timingMilestones) {
        std::cout << "    ";
        printTiming(timing);
        std::cout << '\n';
    }

    std::cout << "cycle_branch_first_difference ";
    if (!lastSuccessfulCycle.valid || !finalCycle.valid) {
        std::cout << "n/a missing_cycle\n";
    } else {
        const auto compareCount = std::min(lastSuccessfulCycle.branchHead.size(), finalCycle.branchHead.size());
        std::optional<std::size_t> diffIndex;
        for (std::size_t i = 0; i < compareCount; ++i) {
            const auto& lhs = lastSuccessfulCycle.branchHead[i];
            const auto& rhs = finalCycle.branchHead[i];
            if (lhs.pc != rhs.pc || lhs.opcode != rhs.opcode || lhs.target != rhs.target ||
                lhs.taken != rhs.taken) {
                diffIndex = i;
                break;
            }
        }
        if (!diffIndex.has_value() && lastSuccessfulCycle.branchHead.size() != finalCycle.branchHead.size()) {
            diffIndex = compareCount;
        }
        if (!diffIndex.has_value()) {
            std::cout << "none branch_sequences_match_in_head\n";
        } else {
            std::cout << "index=" << *diffIndex << '\n';
            if (*diffIndex < lastSuccessfulCycle.branchHead.size()) {
                std::cout << "  success ";
                printBranch(lastSuccessfulCycle.branchHead[*diffIndex]);
                std::cout << '\n';
            } else {
                std::cout << "  success <no branch at this index>\n";
            }
            if (*diffIndex < finalCycle.branchHead.size()) {
                std::cout << "  final ";
                printBranch(finalCycle.branchHead[*diffIndex]);
                std::cout << '\n';
            } else {
                std::cout << "  final <no branch at this index>\n";
            }
        }
    }

    // Compute diffs between the last successful set (snapshot at set) and final stuck set
    std::cout << "ram_diffs_between_successful_clear_and_final_set\n";
    if (!lastSuccessfulCycle.valid || !finalCycle.valid) {
        std::cout << "  no_cycles_found: last_success_valid=" << (lastSuccessfulCycle.valid ? "yes" : "no")
                  << " final_valid=" << (finalCycle.valid ? "yes" : "no") << '\n';
    } else if (!lastSuccessfulCycle.reached4B24) {
        std::cout << "  last_successful_cycle did not reach 4B24/clear; cannot compute between-clear window\n";
    } else {
        const uint64_t windowStart = lastSuccessfulCycle.clearStep;
        const uint64_t windowEnd = finalCycle.setStep;

        std::map<uint16_t, std::pair<uint8_t, uint8_t>> diffsMap;

        auto recordDiffs = [&](uint16_t base, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
            const std::size_t n = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < n; ++i) {
                if (a[i] != b[i]) {
                    const uint16_t addr = static_cast<uint16_t>(base + i);
                    diffsMap[addr] = std::make_pair(a[i], b[i]);
                }
            }
        };

        recordDiffs(0xC700u, lastSuccessfulCycle.setSnapshot.ram_c700_c720, finalCycle.setSnapshot.ram_c700_c720);
        recordDiffs(0xC780u, lastSuccessfulCycle.setSnapshot.ram_c780_c7c0, finalCycle.setSnapshot.ram_c780_c7c0);
        recordDiffs(0xC880u, lastSuccessfulCycle.setSnapshot.ram_c880_c8a0, finalCycle.setSnapshot.ram_c880_c8a0);
        recordDiffs(0xC4E0u, lastSuccessfulCycle.setSnapshot.ram_c4e0_c720, finalCycle.setSnapshot.ram_c4e0_c720);

        // IX-relative: compute base from snapshot.ix - 16
        if (!lastSuccessfulCycle.setSnapshot.ram_ix_range.empty() && !finalCycle.setSnapshot.ram_ix_range.empty()) {
            const int ixBase = static_cast<int>(lastSuccessfulCycle.setSnapshot.ix) - 16;
            const std::size_t n = std::min(lastSuccessfulCycle.setSnapshot.ram_ix_range.size(), finalCycle.setSnapshot.ram_ix_range.size());
            for (std::size_t i = 0; i < n; ++i) {
                const auto a = lastSuccessfulCycle.setSnapshot.ram_ix_range[i];
                const auto b = finalCycle.setSnapshot.ram_ix_range[i];
                if (a != b) diffsMap[static_cast<uint16_t>(ixBase + static_cast<int>(i))] = std::make_pair(a, b);
            }
        }

        // IY-relative: similar
        if (!lastSuccessfulCycle.setSnapshot.ram_iy_range.empty() && !finalCycle.setSnapshot.ram_iy_range.empty()) {
            const int iyBase = static_cast<int>(lastSuccessfulCycle.setSnapshot.iy) - 16;
            const std::size_t n = std::min(lastSuccessfulCycle.setSnapshot.ram_iy_range.size(), finalCycle.setSnapshot.ram_iy_range.size());
            for (std::size_t i = 0; i < n; ++i) {
                const auto a = lastSuccessfulCycle.setSnapshot.ram_iy_range[i];
                const auto b = finalCycle.setSnapshot.ram_iy_range[i];
                if (a != b) diffsMap[static_cast<uint16_t>(iyBase + static_cast<int>(i))] = std::make_pair(a, b);
            }
        }

        if (diffsMap.empty()) {
            std::cout << "  no_ram_diffs_found_in_ranges\n";
        } else {
            // For each diff, find the last writer in the ramWrites window and print enhanced metadata
            for (const auto& entry : diffsMap) {
                const uint16_t addr = entry.first;
                const uint8_t oldv = entry.second.first;
                const uint8_t newv = entry.second.second;
                std::cout << "  addr=" << hex16(addr)
                          << " old=" << hex8(oldv)
                          << " new=" << hex8(newv);

                // determine IX/IY neighborhood membership (based on last successful snapshot)
                const int ixBase = static_cast<int>(lastSuccessfulCycle.setSnapshot.ix) - 16;
                const int ixLen = static_cast<int>(lastSuccessfulCycle.setSnapshot.ram_ix_range.size());
                const int ixStart = ixBase;
                const int ixEnd = ixBase + (ixLen > 0 ? (ixLen - 1) : 0);
                const int iyBase = static_cast<int>(lastSuccessfulCycle.setSnapshot.iy) - 16;
                const int iyLen = static_cast<int>(lastSuccessfulCycle.setSnapshot.ram_iy_range.size());
                const int iyStart = iyBase;
                const int iyEnd = iyBase + (iyLen > 0 ? (iyLen - 1) : 0);
                const bool inIx = (static_cast<int>(addr) >= ixStart && static_cast<int>(addr) <= ixEnd);
                const bool inIy = (static_cast<int>(addr) >= iyStart && static_cast<int>(addr) <= iyEnd);
                std::cout << " in_ix_neighborhood=" << (inIx ? "yes" : "no")
                          << " in_iy_neighborhood=" << (inIy ? "yes" : "no");

                const bool highlight = (addr == 0xC503u || addr == 0xC603u || addr == 0xC703u || addr == 0xC702u);
                if (highlight) {
                    std::cout << " HIGHLIGHT";
                }

                std::optional<RamWriteEvent> lastWriter;
                bool lastWriterFromC700History = false;
                // 1) search ramWrites in the between-clear window
                for (auto it = ramWrites.rbegin(); it != ramWrites.rend(); ++it) {
                    if (it->address == addr && it->step >= windowStart && it->step <= windowEnd) {
                        lastWriter = *it;
                        break;
                    }
                }
                // 2) if not found and address in C700..C710, search small ring history
                if (!lastWriter.has_value() && addr >= 0xC700u && addr <= 0xC710u) {
                    const std::size_t idx = static_cast<std::size_t>(addr - 0xC700u);
                    for (auto it = c700History[idx].rbegin(); it != c700History[idx].rend(); ++it) {
                        if (it->step <= windowEnd && it->step >= windowStart) {
                            lastWriter = *it;
                            lastWriterFromC700History = true;
                            break;
                        }
                    }
                }
                // 3) fallback: if still not found, use lastRamWrite if it existed at or before final set
                if (!lastWriter.has_value()) {
                    const auto it = lastRamWrite.find(addr);
                    if (it != lastRamWrite.end() && it->second.step <= windowEnd) {
                        lastWriter = it->second;
                    }
                }
                if (lastWriter.has_value()) {
                    const auto& w = *lastWriter;
                    std::cout << " last_writer_step=" << w.step << " pc=" << hex16(w.pc)
                              << " prev=" << hex8(w.previousValue)
                              << " written=" << hex8(w.value)
                              << " scanline=" << static_cast<int>(w.scanline)
                              << " machine_irq_pending=" << (w.machine_irq_pending ? "yes" : "no")
                              << " vdp_frame_pending=" << (w.vdp_frame_pending ? "yes" : "no")
                              << " vdp_line_pending=" << (w.vdp_line_pending ? "yes" : "no")
                              << " regs af=" << hex16(w.af) << " bc=" << hex16(w.bc)
                              << " de=" << hex16(w.de) << " hl=" << hex16(w.hl)
                              << " ix=" << hex16(w.ix) << " iy=" << hex16(w.iy)
                              << " sp=" << hex16(w.sp)
                              << " opcodes";
                    printOpcodeWindow(w.opcodes);
                    if (lastWriterFromC700History) std::cout << " (from_c700_history)";
                    std::cout << '\n';
                } else {
                    std::cout << " last_writer=n/a\n";
                }
            }
        }
    }

    return 0;
}
