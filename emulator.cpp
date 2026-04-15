/////////////////////////////////////////////////////////////////////////
//
//	2020 Emulator Project Idea Mk 2
//	Author: Brandon M. M. Green
//
//	/////
//
// 	The purpose of this is to see the vision of this emulator system
//
//
/////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"
#include "machine/TimingService.hpp"

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void handleSignal(int)
{
    gStopRequested = 1;
}

struct EmulatorOptions {
    std::filesystem::path romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::filesystem::path> pluginPath;
    std::optional<std::uint64_t> stepLimit;
    int windowScale = 3;
    bool headless = false;
    bool unthrottled = false;
    double speedMultiplier = 1.0;
    bool startPaused = false;
};

void printUsage(std::string_view program)
{
    std::cerr << "Usage: " << program << " --rom <path.gb> [options]\n"
              << "   or: " << program << " <path.gb> [options]\n\n"
              << "Options:\n"
              << "  --rom <path>       Cartridge ROM to load\n"
              << "  --boot-rom <path>  Optional 256-byte DMG boot ROM\n"
              << "  --plugin <path>    Optional SDL frontend shared object override\n"
              << "  --steps <count>    Stop after a fixed number of instruction steps\n"
              << "  --scale <n>        SDL window scale factor (default: 3)\n"
              << "  --unthrottled      Run unthrottled (no wall-clock pacing)\n"
              << "  --speed <mult>     Start with speed multiplier (e.g. 2.0)\n"
              << "  --pause            Start paused (use single-step to advance)\n"
              << "  --headless         Run without the SDL frontend plugin\n"
              << "  -h, --help         Show this help text\n\n"
              << "Controls:\n"
              << "  Arrow keys = D-pad, Z = A, X = B, Backspace = Select, Enter = Start\n";
}

void assignRomPath(EmulatorOptions& options, const char* value)
{
    if (!options.romPath.empty()) {
        throw std::invalid_argument("ROM path was provided more than once");
    }
    options.romPath = value;
}

std::uint64_t parseUnsignedArgument(const char* value, std::string_view optionName)
{
    try {
        std::size_t parsedChars = 0;
        const auto parsedValue = std::stoull(value, &parsedChars, 0);
        if (parsedChars != std::string(value).size()) {
            throw std::invalid_argument("trailing characters");
        }
        return static_cast<std::uint64_t>(parsedValue);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid value for " + std::string(optionName) + ": " + value);
    }
}

EmulatorOptions parseArguments(int argc, char** argv)
{
    EmulatorOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rom") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--rom requires a path");
            }
            assignRomPath(options, argv[++i]);
        } else if (arg == "--boot-rom") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--boot-rom requires a path");
            }
            options.bootRomPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--plugin") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--plugin requires a path");
            }
            options.pluginPath = std::filesystem::path(argv[++i]);
        } else if (arg == "--steps") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--steps requires a count");
            }
            options.stepLimit = parseUnsignedArgument(argv[++i], "--steps");
        } else if (arg == "--scale") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--scale requires a positive integer");
            }
            options.windowScale = static_cast<int>(std::max<std::uint64_t>(1u, parseUnsignedArgument(argv[++i], "--scale")));
        } else if (arg == "--unthrottled") {
            options.unthrottled = true;
        } else if (arg == "--speed") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--speed requires a numeric multiplier");
            }
            try {
                options.speedMultiplier = std::stod(argv[++i]);
            } catch (...) {
                throw std::invalid_argument("Invalid value for --speed: " + std::string(argv[i]));
            }
        } else if (arg == "--pause") {
            options.startPaused = true;
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(EXIT_SUCCESS);
        } else if (!arg.empty() && arg.front() == '-') {
            throw std::invalid_argument("Unknown option: " + arg);
        } else {
            assignRomPath(options, argv[i]);
        }
    }

    if (options.romPath.empty()) {
        throw std::invalid_argument("Missing ROM path. Use --rom <file.gb>.");
    }

    return options;
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file: " + path.string());
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        throw std::runtime_error("File is empty: " + path.string());
    }

    return bytes;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options = parseArguments(argc, argv);

        std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
        std::signal(SIGTERM, handleSignal);
#endif

        GameBoyMachine machine;

        if (options.bootRomPath.has_value()) {
            machine.loadBootRom(readBinaryFile(*options.bootRomPath));
        }

        const auto romBytes = readBinaryFile(options.romPath);
        machine.loadRom(romBytes);

        BMMQ::ISdlFrontendPlugin* frontend = nullptr;
        std::unique_ptr<BMMQ::ISdlFrontendPlugin> frontendPlugin;
        if (!options.headless) {
            BMMQ::SdlFrontendConfig config;
            config.windowTitle = "Proto-Time - " + options.romPath.filename().string();
            config.windowScale = std::max(options.windowScale, 1);
            config.autoInitializeBackend = true;
            config.createHiddenWindowOnInitialize = true;
            config.pumpBackendEventsOnInputSample = false;
            config.autoPresentOnVideoEvent = false;
            config.showWindowOnPresent = true;

            const auto pluginPath = options.pluginPath.value_or(
                BMMQ::defaultSdlFrontendPluginPath((argc > 0 && argv != nullptr)
                    ? std::filesystem::path(argv[0])
                    : std::filesystem::path("timeEmulator")));
            try {
                frontendPlugin = BMMQ::loadSdlFrontendPlugin(pluginPath, config);
                frontend = frontendPlugin.get();
                machine.pluginManager().add(std::move(frontendPlugin));
                machine.pluginManager().initialize(machine.mutableView());
                frontend->requestWindowVisibility(true);
                frontend->serviceFrontend();
            } catch (const std::exception& ex) {
                std::cerr << "warning: " << ex.what() << "; continuing headless\n";
            }
        }

        std::cout << "Loaded ROM: " << options.romPath << " (" << romBytes.size() << " bytes)\n";
        if (options.bootRomPath.has_value()) {
            std::cout << "Loaded boot ROM: " << *options.bootRomPath << '\n';
        }
        if (frontend != nullptr) {
            std::cout << "Frontend: " << frontend->backendStatusSummary() << '\n';
        }
        if (options.stepLimit.has_value()) {
            std::cout << "Running for " << *options.stepLimit << " instruction steps\n";
        } else {
            std::cout << "Running until the window is closed or Ctrl+C is pressed\n";
        }

        std::uint64_t steps = 0;
        const auto cpuClockHz = machine.clockHz();
        using SteadyClock = std::chrono::steady_clock;
        constexpr auto kFrontendServicePeriod = std::chrono::milliseconds(1);
        constexpr auto kMaxCatchUpWindow = std::chrono::milliseconds(8);
        constexpr auto kMinSleepQuantum = std::chrono::milliseconds(1);
        const double kMinInstructionCycles = 4.0;
        const double kExecutionSliceSeconds = 0.001;
        const double kFrontendServiceSliceSeconds = 0.001;

        BMMQ::TimingService timingService;
        BMMQ::TimingConfig timingConfig;
        timingConfig.baseClockHz = static_cast<double>(cpuClockHz);
        timingConfig.speedMultiplier = options.speedMultiplier;
        timingConfig.minInstructionCycles = kMinInstructionCycles;
        timingConfig.executionSliceSeconds = kExecutionSliceSeconds;
        timingConfig.frontendServiceSliceSeconds = kFrontendServiceSliceSeconds;
        timingConfig.maxCatchUp = kMaxCatchUpWindow;
        timingConfig.minSleepQuantum = kMinSleepQuantum;
        timingConfig.throttled = !options.unthrottled;
        timingService.configure(timingConfig);
        BMMQ::TimingEngine timingEngine(timingConfig);

        auto initialNow = SteadyClock::now();
        auto nextFrontendService = initialNow + kFrontendServicePeriod;
        timingService.start(initialNow);
        timingEngine.start(initialNow);
        if (options.startPaused) {
            timingService.setPaused(true);
        }

        auto serviceFrontend = [&]() -> bool {
            if (frontend == nullptr) {
                return false;
            }
            frontend->serviceFrontend();
            return frontend->quitRequested();
        };

        auto serviceFrontendUntil = [&](SteadyClock::time_point now) -> bool {
            bool servicedFrontend = false;
            if (frontend == nullptr || now < nextFrontendService) {
                return false;
            }
            if (now - nextFrontendService > kMaxCatchUpWindow) {
                nextFrontendService = now;
            }
            do {
                servicedFrontend = true;
                if (serviceFrontend()) {
                    return true;
                }
                nextFrontendService += kFrontendServicePeriod;
            } while (nextFrontendService <= now);
            if (servicedFrontend) {
                timingEngine.applyControl(timingService.takeControlSnapshot());
            }
            return false;
        };

        auto audioBackpressureActive = [&]() -> bool {
            return frontend != nullptr && frontend->audioQueueBackpressureActive();
        };

        while (gStopRequested == 0) {
            if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                break;
            }

            const auto now = SteadyClock::now();
            if (serviceFrontendUntil(now)) {
                break;
            }

            timingEngine.applyControl(timingService.takeControlSnapshot());
            timingEngine.update(now);

            bool executedInstruction = false;
            bool executionSliceActive = false;
            while (!audioBackpressureActive() && timingEngine.canExecute() && gStopRequested == 0) {
                if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                    break;
                }

                if (!executionSliceActive) {
                    timingEngine.beginExecutionSlice();
                    executionSliceActive = true;
                }

                machine.step();
                ++steps;
                executedInstruction = true;

                const auto retiredCycles = static_cast<double>(machine.runtimeContext().getLastFeedback().retiredCycles);
                const auto chargedCycles = std::max(kMinInstructionCycles, retiredCycles);
                timingEngine.charge(retiredCycles);
                const auto sliceDecision = timingEngine.recordExecutionSliceCycles(chargedCycles);

                if (sliceDecision.frontendServiceDue && serviceFrontendUntil(SteadyClock::now())) {
                    gStopRequested = 1;
                    break;
                }
                if (sliceDecision.executionSliceComplete) {
                    break;
                }
            }
            timingService.publishEngineStats(timingEngine.stats());

            if (gStopRequested != 0) {
                break;
            }

            const auto idleNow = SteadyClock::now();
            if (serviceFrontendUntil(idleNow)) {
                break;
            }

            if (!executedInstruction) {
                const auto nextStepTime = timingEngine.nextWakeTime(idleNow);
                const auto audioBackpressureWakeTime = audioBackpressureActive()
                    ? idleNow + std::chrono::milliseconds(1)
                    : idleNow;
                const auto frontendWakeTime = (frontend != nullptr) ? nextFrontendService : idleNow;
                auto nextWakeTime = (frontend != nullptr)
                    ? std::min(frontendWakeTime, nextStepTime)
                    : nextStepTime;
                if (audioBackpressureWakeTime > idleNow) {
                    nextWakeTime = (nextWakeTime > idleNow)
                        ? std::min(nextWakeTime, audioBackpressureWakeTime)
                        : audioBackpressureWakeTime;
                }

                const bool frontendSleepDue = (frontend != nullptr) && (frontendWakeTime > idleNow);
                const bool audioBackpressureSleepDue = audioBackpressureWakeTime > idleNow;
                const bool timingSleepDue = timingEngine.shouldSleep(idleNow) && (nextStepTime > idleNow);

                if (timingSleepDue || frontendSleepDue || audioBackpressureSleepDue) {
                    if (nextWakeTime > idleNow) {
                        std::this_thread::sleep_until(nextWakeTime);
                    }
                }
            }
        }

        serviceFrontend();

        const auto pc = machine.runtimeContext().readRegister16(GB::RegisterId::PC);
        const auto ly = machine.runtimeContext().read8(0xFF44);
        const auto lcdc = machine.runtimeContext().read8(0xFF40);
        const auto stat = machine.runtimeContext().read8(0xFF41);
        const auto interruptFlags = machine.runtimeContext().read8(0xFF0F);
        const auto interruptEnable = machine.runtimeContext().read8(0xFFFF);

        std::cout << "Stopped after " << steps
                  << " instruction steps at PC=0x"
                  << std::hex << std::uppercase << pc << std::dec << '\n';
        std::cout << "I/O state: LY=0x" << std::hex << std::uppercase << static_cast<int>(ly)
                  << " LCDC=0x" << static_cast<int>(lcdc)
                  << " STAT=0x" << static_cast<int>(stat)
                  << " IF=0x" << static_cast<int>(interruptFlags)
                  << " IE=0x" << static_cast<int>(interruptEnable)
                  << std::dec << '\n';
        return EXIT_SUCCESS;
    } catch (const std::invalid_argument& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        printUsage((argc > 0 && argv != nullptr) ? argv[0] : "timeEmulator");
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
