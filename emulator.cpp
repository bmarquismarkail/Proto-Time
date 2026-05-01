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
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "emulator/EmulatorConfig.hpp"
#include "emulator/EmulatorHost.hpp"
#include "machine/BackgroundTaskService.hpp"
#include "machine/plugins/SdlFrontendPluginLoader.hpp"
#include "machine/TimingService.hpp"
#include "cores/gameboy/GameBoyMachine.hpp"

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void handleSignal(int)
{
    gStopRequested = 1;
}

void printUsage(std::string_view program)
{
    std::cerr << "Usage: " << program << " --core <gameboy|gamegear> --rom <path> [options]\n"
              << "   or: " << program << " --core <gameboy|gamegear> <path> [options]\n\n"
              << "Options:\n"
              << "  --core <name>      Machine core to run: gameboy or gamegear\n"
              << "  --config <path>    Optional INI-style emulator configuration file\n"
              << "  --rom <path>       Cartridge ROM to load\n"
              << "  --boot-rom <path>  Optional external boot ROM for supported cores\n"
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
              << "  Arrow keys = directions, Z = Button1, X = Button2,\n"
              << "  Backspace = Meta1, Enter = Meta2\n";
}

std::filesystem::file_time_type fileWriteTime(const std::filesystem::path& path) noexcept
{
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::filesystem::file_time_type::min();
    }
    return writeTime;
}

struct VisualReloadPollState {
    std::mutex mutex{};
    std::vector<std::filesystem::path> watchedPaths{};
    std::map<std::string, std::filesystem::file_time_type> lastWriteTimes{};
    std::atomic<bool> pollInFlight{false};
    std::atomic<bool> reloadRequested{false};
    std::chrono::steady_clock::time_point nextPollDue{};
};

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

        BMMQ::BackgroundTaskService backgroundTaskService;
        backgroundTaskService.start();

        auto bootstrapped = BMMQ::bootstrapMachine(options);
        auto& machine = *bootstrapped.machine;
        const auto& descriptor = bootstrapped.descriptor;
        const auto romSize = bootstrapped.romSize;
        if (auto* gameBoyMachine = dynamic_cast<GameBoyMachine*>(bootstrapped.machine.get());
            gameBoyMachine != nullptr) {
            gameBoyMachine->setBackgroundTaskService(&backgroundTaskService);
        }

        BMMQ::ISdlFrontendPlugin* frontend = nullptr;
        std::unique_ptr<BMMQ::ISdlFrontendPlugin> frontendPlugin;
        if (!options.headless) {
            BMMQ::SdlFrontendConfig config;
            config.windowTitle = "Proto-Time - " + std::string(descriptor.displayName) +
                " - " + options.romPath.filename().string();
            config.windowScale = std::max(options.windowScale, 1u);
            config.frameWidth = descriptor.defaultFrameWidth;
            config.frameHeight = descriptor.defaultFrameHeight;
            config.autoInitializeBackend = true;
            // The SDL presenter opens windows hidden; ensure frames are
            // presented automatically so the window appears during normal runs.
            config.createHiddenWindowOnInitialize = false;
            config.pumpBackendEventsOnInputSample = false;
            config.autoPresentOnVideoEvent = true;
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

        std::cout << "Core: " << descriptor.id << '\n';
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
        if (options.timingProfile.has_value()) {
            std::cout << "Timing profile: " << *options.timingProfile << '\n';
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
        constexpr std::uint32_t kMaxFrontendServiceTicksPerWake = 2u;
        const double kMinInstructionCycles = 4.0;
        const double kExecutionSliceSeconds = 0.001;
        const double kFrontendServiceSliceSeconds = 0.001;

        BMMQ::TimingService timingService;
        BMMQ::TimingConfig timingConfig;
        timingConfig.baseClockHz = static_cast<double>(cpuClockHz);
        const auto timingProfile = options.timingProfile.has_value()
            ? BMMQ::parseTimingPolicyProfile(*options.timingProfile)
            : BMMQ::TimingPolicyProfile::Balanced;
        BMMQ::applyTimingPolicyProfileDefaults(timingProfile, timingConfig);
        timingConfig.speedMultiplier = options.speedMultiplier;
        timingConfig.minInstructionCycles = kMinInstructionCycles;
        timingConfig.executionSliceSeconds = kExecutionSliceSeconds;
        timingConfig.frontendServiceSliceSeconds = kFrontendServiceSliceSeconds;
        timingConfig.maxCatchUp = kMaxCatchUpWindow;
        if (!options.timingProfile.has_value()) {
            timingConfig.minSleepQuantum = kMinSleepQuantum;
        }
        timingConfig.maxCyclesPerWake = static_cast<double>(cpuClockHz) * 0.004;
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

        VisualReloadPollState visualReloadPollState;
        constexpr auto kVisualReloadPollInterval = std::chrono::milliseconds(125);

        auto refreshVisualReloadWatchList = [&]() {
            if (!options.visualPackReload) {
                return;
            }
            auto watchedPaths = machine.visualOverrideService().watchedReloadPaths();
            if (watchedPaths.empty()) {
                watchedPaths = options.visualPackPaths;
            }

            std::lock_guard<std::mutex> lock(visualReloadPollState.mutex);
            visualReloadPollState.watchedPaths = std::move(watchedPaths);

            std::map<std::string, std::filesystem::file_time_type> refreshed;
            for (const auto& watchedPath : visualReloadPollState.watchedPaths) {
                const auto key = watchedPath.lexically_normal().string();
                refreshed[key] = fileWriteTime(watchedPath);
            }
            visualReloadPollState.lastWriteTimes = std::move(refreshed);
        };

        auto scheduleVisualReloadProbe = [&](SteadyClock::time_point now) {
            if (!options.visualPackReload || now < visualReloadPollState.nextPollDue) {
                return;
            }
            visualReloadPollState.nextPollDue = now + kVisualReloadPollInterval;

            if (visualReloadPollState.pollInFlight.exchange(true, std::memory_order_acq_rel)) {
                return;
            }

            const bool queued = backgroundTaskService.submit([&visualReloadPollState]() {
                bool changed = false;
                {
                    std::lock_guard<std::mutex> lock(visualReloadPollState.mutex);
                    for (const auto& watchedPath : visualReloadPollState.watchedPaths) {
                        const auto key = watchedPath.lexically_normal().string();
                        const auto currentWriteTime = fileWriteTime(watchedPath);
                        const auto it = visualReloadPollState.lastWriteTimes.find(key);
                        if (it == visualReloadPollState.lastWriteTimes.end()) {
                            visualReloadPollState.lastWriteTimes.emplace(key, currentWriteTime);
                            continue;
                        }
                        if (it->second != currentWriteTime) {
                            it->second = currentWriteTime;
                            changed = true;
                        }
                    }
                }

                if (changed) {
                    visualReloadPollState.reloadRequested.store(true, std::memory_order_release);
                }
                visualReloadPollState.pollInFlight.store(false, std::memory_order_release);
            });

            if (!queued) {
                visualReloadPollState.pollInFlight.store(false, std::memory_order_release);
            }
        };

        refreshVisualReloadWatchList();

        auto pollVisualPackReload = [&]() {
            if (!options.visualPackReload) {
                return;
            }
            scheduleVisualReloadProbe(SteadyClock::now());
            if (!visualReloadPollState.reloadRequested.exchange(false, std::memory_order_acq_rel)) {
                return;
            }

            const bool reloaded = machine.visualOverrideService().reloadChangedPacks();
            refreshVisualReloadWatchList();
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

            const auto lateness = now - nextFrontendService;
            std::uint32_t scheduledTicks = 1u;
            if (lateness > kFrontendServicePeriod) {
                const auto behindPeriods =
                    static_cast<std::uint32_t>(lateness / kFrontendServicePeriod);
                scheduledTicks += behindPeriods;
            }
            std::uint32_t executedTicks = 0u;
            const auto ticksToRun = std::min<std::uint32_t>(scheduledTicks, kMaxFrontendServiceTicksPerWake);
            const auto mergedTicks = scheduledTicks - ticksToRun;

            for (std::uint32_t tick = 0u; tick < ticksToRun; ++tick) {
                servicedFrontend = true;
                ++executedTicks;
                if (serviceFrontend()) {
                    timingService.noteFrontendServiceTick(scheduledTicks, executedTicks, lateness);
                    return true;
                }
                nextFrontendService += kFrontendServicePeriod;
            }
            if (mergedTicks != 0u) {
                nextFrontendService += kFrontendServicePeriod * mergedTicks;
            }
            if (now - nextFrontendService > kMaxCatchUpWindow) {
                nextFrontendService = now + kFrontendServicePeriod;
            }
            timingService.noteFrontendServiceTick(scheduledTicks, executedTicks, lateness);
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
            std::uint32_t wakeExecutionSlices = 0u;
            double wakeExecutionCycles = 0.0;
            while (timingEngine.canExecute() && gStopRequested == 0) {
                if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                    break;
                }
                if (wakeExecutionSlices >= timingConfig.maxExecutionSlicesPerWake) {
                    timingService.noteWakeBurstSliceLimitHit();
                    break;
                }
                if (wakeExecutionCycles >= timingConfig.maxCyclesPerWake) {
                    timingService.noteWakeBurstCycleLimitHit();
                    break;
                }

                if (!executionSliceActive) {
                    timingEngine.beginExecutionSlice();
                    executionSliceActive = true;
                    ++wakeExecutionSlices;
                }

                machine.step();
                ++steps;
                executedInstruction = true;

                const auto retiredCycles = static_cast<double>(machine.runtimeContext().getLastFeedback().retiredCycles);
                const auto chargedCycles = std::max(kMinInstructionCycles, retiredCycles);
                wakeExecutionCycles += chargedCycles;
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
            timingService.recordWakeBurst(wakeExecutionCycles, wakeExecutionSlices);
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
                        const auto requestedSleep = std::chrono::duration_cast<std::chrono::nanoseconds>(nextWakeTime - idleNow);
                        const auto beforeSleep = SteadyClock::now();
                        if (timingConfig.adaptiveSleepEnabled && requestedSleep > timingConfig.sleepSpinWindow &&
                            timingConfig.sleepSpinWindow > std::chrono::nanoseconds::zero()) {
                            const auto coarseWake = nextWakeTime - timingConfig.sleepSpinWindow;
                            std::this_thread::sleep_until(coarseWake);
                            const auto spinStart = SteadyClock::now();
                            while (SteadyClock::now() < nextWakeTime) {
                                if (SteadyClock::now() - spinStart >= timingConfig.sleepSpinCap) {
                                    break;
                                }
                                std::this_thread::yield();
                            }
                        } else {
                            std::this_thread::sleep_until(nextWakeTime);
                        }
                        const auto afterSleep = SteadyClock::now();
                        const auto actualSleep = std::chrono::duration_cast<std::chrono::nanoseconds>(afterSleep - beforeSleep);
                        timingService.noteHostSleep(requestedSleep, actualSleep);
                    }
                }
            }
        }

        serviceFrontend();
        if (!options.visualPackPaths.empty() || options.visualCapturePath.has_value()) {
            (void)machine.visualOverrideService().captureStats();
            std::cout << machine.visualOverrideService().authorDiagnosticsReport();
        }

        std::cout << "Stopped after " << steps << " instruction steps";
        const auto stopSummary = machine.stopSummary();
        if (!stopSummary.empty()) {
            std::cout << ":\n" << stopSummary << '\n';
        } else {
            std::cout << '\n';
        }
        backgroundTaskService.shutdown();
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
