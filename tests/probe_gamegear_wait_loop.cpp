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
};

struct MemEvent {
    uint64_t step = 0;
    uint16_t pc = 0;
    uint16_t address = 0;
    uint8_t value = 0;
    uint8_t scanline = 0;
};

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
    std::map<uint8_t, uint64_t> readCounts;
    std::map<uint8_t, uint64_t> writeCounts;
    std::map<uint8_t, uint8_t> lastRead;
    std::map<uint8_t, uint8_t> lastWrite;
    bool interruptRequested = false;
    uint64_t interruptProviderCalls = 0;
    uint64_t interruptProviderServed = 0;
    uint64_t currentStep = 0;

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
        events.push_back(IoEvent{
            currentStep,
            cpu.PC,
            kind,
            port,
            value,
            vdp.currentScanline(),
        });
        if (events.size() > tailLimit) {
            events.erase(events.begin());
        }
    };
    auto recordMemWrite = [&](uint16_t address, uint8_t value) {
        if (!((address >= 0xC100u && address <= 0xC1A0u) ||
              (address >= 0xC700u && address <= 0xC710u))) {
            return;
        }
        memEvents.push_back(MemEvent{
            currentStep,
            cpu.PC,
            address,
            value,
            vdp.currentScanline(),
        });
        if (memEvents.size() > tailLimit) {
            memEvents.erase(memEvents.begin());
        }
    };

    cpu.setMemoryInterface(
        [&](uint16_t address) { return mem.read(address); },
        [&](uint16_t address, uint8_t value) {
            mem.write(address, value);
            recordMemWrite(address, value);
        });
    cpu.setIoInterface(
        [&](uint8_t port) {
            const auto value = mem.readIoPort(port);
            record('R', port, value);
            return value;
        },
        [&](uint8_t port, uint8_t value) {
            mem.writeIoPort(port, value);
            record('W', port, value);
        });
    cpu.setInterruptRequestProvider([&]() -> std::optional<uint8_t> {
        ++interruptProviderCalls;
        if (interruptRequested) {
            interruptRequested = false;
            ++interruptProviderServed;
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
        ++pcCounts[cpu.PC];
        recentPcs[recentPcIndex++ % recentPcs.size()] = cpu.PC;
        const auto cycles = cpu.step();
        vdp.step(cycles);
        psg.step(cycles);
        if (vdp.takeIrqAsserted()) {
            interruptRequested = true;
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

    return 0;
}
