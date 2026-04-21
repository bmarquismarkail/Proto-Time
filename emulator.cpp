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
#include "emulator/EmulatorConfig.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"
#include "machine/TimingService.hpp"

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void handleSignal(int)
{
    gStopRequested = 1;
}

void printUsage(std::string_view program)
{
    std::cerr << "Usage: " << program << " --rom <path.gb> [options]\n"
              << "   or: " << program << " <path.gb> [options]\n\n"
              << "Options:\n"
              << "  --config <path>    Optional INI-style emulator configuration file\n"
              << "  --rom <path>       Cartridge ROM to load\n"
              << "  --boot-rom <path>  Optional 256-byte DMG boot ROM\n"
              << "  --plugin <path>    Optional SDL frontend shared object override\n"
              << "  --steps <count>    Stop after a fixed number of instruction steps\n"
              << "  --scale <n>        SDL window scale factor (default: 3)\n"
              << "  --unthrottled      Run unthrottled (no wall-clock pacing)\n"
              << "  --speed <mult>     Start with speed multiplier (e.g. 2.0)\n"
              << "  --pause            Start paused (use single-step to advance)\n"
              << "  --no-audio         Disable frontend audio output\n"
              << "  --audio-backend <name>\n"
              << "                     Frontend audio backend: sdl, dummy, or file (default: sdl)\n"
              << "  --visual-pack <path>\n"
              << "                     Load a visual override pack.json; repeat to load multiple packs\n"
              << "  --visual-capture <dir>\n"
              << "                     Capture observed decoded visual resources for pack authoring\n"
              << "  --visual-pack-reload\n"
              << "                     Poll visual pack manifests/assets and reload changed packs\n"
              << "  --headless         Run without the SDL frontend plugin\n"
              << "  -h, --help         Show this help text\n\n"
              << "Controls:\n"
              << "  Arrow keys = D-pad, Z = A, X = B, Backspace = Select, Enter = Start\n";
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
        const auto parsedArguments = BMMQ::parseEmulatorArguments(argc, argv);
        if (parsedArguments.helpRequested) {
            printUsage((argc > 0 && argv != nullptr) ? argv[0] : "timeEmulator");
            return EXIT_SUCCESS;
        }
        const auto options = BMMQ::resolveEmulatorConfig(parsedArguments);

        std::signal(SIGINT, handleSignal);
#ifdef SIGTERM
        std::signal(SIGTERM, handleSignal);
#endif

        GameBoyMachine machine;

        if (options.bootRomPath.has_value()) {
            machine.loadBootRom(readBinaryFile(*options.bootRomPath));
        }

        std::size_t romSize = machine.loadRomFromPath(options.romPath);

        for (const auto& visualPackPath : options.visualPackPaths) {
            if (!machine.visualOverrideService().loadPackManifest(visualPackPath)) {
                throw std::runtime_error(
                    "Unable to load visual pack: " + visualPackPath.string() +
                    " (" + machine.visualOverrideService().lastError() + ")");
            }
        }

        bool captureStarted = false;
        if (options.visualCapturePath.has_value()) {
            if (!machine.visualOverrideService().beginCapture(*options.visualCapturePath, "gameboy")) {
                throw std::runtime_error(
                    "Unable to start visual resource capture: " + options.visualCapturePath->string() +
                    " (" + machine.visualOverrideService().lastError() + ")");
            }
            captureStarted = true;
        }

        BMMQ::ISdlFrontendPlugin* frontend = nullptr;
        std::unique_ptr<BMMQ::ISdlFrontendPlugin> frontendPlugin;
        if (!options.headless) {
            BMMQ::SdlFrontendConfig config;
            config.windowTitle = "Proto-Time - " + options.romPath.filename().string();
            config.windowScale = std::max(options.windowScale, 1u);
            config.autoInitializeBackend = true;
            config.createHiddenWindowOnInitialize = true;
            config.pumpBackendEventsOnInputSample = false;
            config.autoPresentOnVideoEvent = false;
            config.showWindowOnPresent = true;
            config.enableAudio = options.audioEnabled;
            config.audioBackend = options.audioBackend;

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

        std::cout << "Loaded ROM: " << options.romPath << " ("
            << romSize << " bytes)\n";
        if (options.bootRomPath.has_value()) {
            std::cout << "Loaded boot ROM: " << *options.bootRomPath << '\n';
        }
        for (const auto& visualPackPath : options.visualPackPaths) {
            std::cout << "Loaded visual pack: " << visualPackPath << '\n';
        }
        if (options.visualPackReload) {
            std::cout << "Visual pack reload polling enabled\n";
        }
        if (options.visualCapturePath.has_value()) {
            std::cout << "Capturing visual resources to: " << *options.visualCapturePath << '\n';
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

        auto pollVisualPackReload = [&]() {
            if (!options.visualPackReload) {
                return;
            }
            const bool reloaded = machine.visualOverrideService().reloadChangedPacks();
            if (!reloaded) {
                if (const auto warning = machine.visualOverrideService().takeReloadWarning(); warning.has_value()) {
                    std::cerr << "warning: " << *warning << '\n';
                }
            }
        };

        auto serviceFrontend = [&]() -> bool {
            if (frontend == nullptr) {
                return false;
            }
            frontend->serviceFrontend();
            pollVisualPackReload();
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
                machine.serviceInput();
            }
            return false;
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
            while (timingEngine.canExecute() && gStopRequested == 0) {
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
            if (frontend == nullptr) {
                pollVisualPackReload();
            }

            if (!executedInstruction) {
                const auto nextStepTime = timingEngine.nextWakeTime(idleNow);
                const auto frontendWakeTime = (frontend != nullptr) ? nextFrontendService : idleNow;
                const auto nextWakeTime = (frontend != nullptr)
                    ? std::min(frontendWakeTime, nextStepTime)
                    : nextStepTime;

                const bool frontendSleepDue = (frontend != nullptr) && (frontendWakeTime > idleNow);
                const bool timingSleepDue = timingEngine.shouldSleep(idleNow) && (nextStepTime > idleNow);

                if (timingSleepDue || frontendSleepDue) {
                    if (nextWakeTime > idleNow) {
                        std::this_thread::sleep_until(nextWakeTime);
                    }
                }
            }
        }

        serviceFrontend();
        if (captureStarted) {
            machine.visualOverrideService().endCapture();
        }
        if (!options.visualPackPaths.empty() || options.visualCapturePath.has_value()) {
            (void)machine.visualOverrideService().captureStats();
            std::cout << machine.visualOverrideService().authorDiagnosticsReport();
        }

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
