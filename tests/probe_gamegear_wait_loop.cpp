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

struct MemEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t address = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
};

struct OpcodeWindow {
    uint16_t start = 0;
    std::array<uint8_t, 17> bytes{};
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

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--steps" && i + 1 < argc) {
            maxSteps = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--press-start-at" && i + 1 < argc) {
            pressStartAt = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--tail" && i + 1 < argc) {
            tailLimit = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
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
    std::vector<C702WriteEvent> c702Writes;
    std::vector<C702ReadEvent> c702Reads;
    std::vector<InterruptEvent> interruptEvents;
    std::vector<VdpRegisterWriteEvent> vdpRegisterWrites;
    std::vector<IoEvent> bfStatusReads;
    std::vector<CpuSnapshot> tracePcHits;
    std::vector<BranchEvent> branchEvents;
    std::vector<BranchEvent> branchNeighborhoodEvents;
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

    auto bankState = [&]() {
        BankState state{};
        if (debugCart != nullptr) {
            state.available = true;
            state.registers = debugCart->bankRegisters();
            state.control = debugCart->controlRegister();
        }
        return state;
    };
    auto snapshotCpu = [&](uint16_t pc, uint8_t before, uint8_t after) {
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
            captureOpcodeWindow(mem, pc),
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
        if (!((address >= 0xC100u && address <= 0xC1A0u) ||
              (address >= 0xC700u && address <= 0xC710u))) {
            return;
        }
        pushTail(memEvents, MemEvent{
            currentStep,
            currentInstructionPc,
            address,
            value,
            vdp.currentScanline(),
        }, tailLimit);
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
            return value;
        },
        [&](uint16_t address, uint8_t value) {
            mem.write(address, value);
            recordMemWrite(address, value);
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
        auto preStepTiming = captureTiming(activeCycle.valid ? "after_4500" : "outside_cycle",
                                           "pc_before_execute",
                                           lastPc,
                                           lastPc);
        if (lastPc == 0x4090u || lastPc == 0x4095u ||
            (lastPc == 0x4516u && activeCycle.valid)) {
            preStepTiming.label = lastPc == 0x4090u ? "pc_4090_before" :
                                  lastPc == 0x4095u ? "pc_4095_before" :
                                                       "ret_after_4500_before";
            recordTiming(preStepTiming);
        }
        const auto cycles = cpu.step();
        totalCpuCycles += cycles;
        const auto postStepSnapshot = snapshotCpu(lastPc, c702BeforeStep, c702Value);
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
            preStepTiming.cycleName = "after_4500";
            preStepTiming.label = "pc_4500_before";
            recordTiming(preStepTiming);
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
                  << '\n';
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

    return 0;
}
