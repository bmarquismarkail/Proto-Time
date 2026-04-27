#define private public
#include "cores/gamegear/Z80Interpreter.hpp"
#undef private

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

template <typename T>
void pushTail(std::vector<T>& events, const T& event, std::size_t limit) {
    events.push_back(event);
    if (events.size() > limit) {
        events.erase(events.begin());
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

bool isVdpDataPort(uint8_t port) {
    return (port & 0xC1u) == 0x80u;
}

bool isVdpControlPort(uint8_t port) {
    return (port & 0xC1u) == 0x81u;
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
            const auto value = mem.readIoPort(port);
            observeIoAccess('R', port, value);
            record('R', port, value);
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
        const uint8_t opcodeAtPc = mem.read(cpu.PC);
        const uint8_t nextOpcodeByte = mem.read(static_cast<uint16_t>(cpu.PC + 1u));
        const auto cycles = cpu.step();
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
        }
        vdp.step(cycles);
        psg.step(cycles);
        if (vdp.takeIrqAsserted()) {
            interruptRequested = true;
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

    return 0;
}
