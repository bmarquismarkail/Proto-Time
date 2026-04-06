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
#include "machine/plugins/SdlFrontendPlugin.hpp"

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

void handleSignal(int)
{
    gStopRequested = 1;
}

struct EmulatorOptions {
    std::filesystem::path romPath;
    std::optional<std::filesystem::path> bootRomPath;
    std::optional<std::uint64_t> stepLimit;
    int windowScale = 3;
    bool headless = false;
};

void printUsage(std::string_view program)
{
    std::cerr << "Usage: " << program << " --rom <path.gb> [options]\n"
              << "   or: " << program << " <path.gb> [options]\n\n"
              << "Options:\n"
              << "  --rom <path>       Cartridge ROM to load\n"
              << "  --boot-rom <path>  Optional 256-byte DMG boot ROM\n"
              << "  --steps <count>    Stop after a fixed number of instruction steps\n"
              << "  --scale <n>        SDL window scale factor (default: 3)\n"
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

        BMMQ::SdlFrontendPlugin* frontend = nullptr;
        if (!options.headless) {
            BMMQ::SdlFrontendConfig config;
            config.windowTitle = "Proto-Time - " + options.romPath.filename().string();
            config.windowScale = std::max(options.windowScale, 1);
            config.autoInitializeBackend = true;
            config.createHiddenWindowOnInitialize = true;
            config.autoPresentOnVideoEvent = true;
            config.showWindowOnPresent = true;

            auto frontendPlugin = std::make_unique<BMMQ::SdlFrontendPlugin>(config);
            frontend = frontendPlugin.get();
            machine.pluginManager().add(std::move(frontendPlugin));
            machine.pluginManager().initialize(machine.view());
            frontend->requestWindowVisibility(true);
            frontend->serviceFrontend();
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
        constexpr std::uint64_t kFrontendServiceInterval = 512;
        constexpr std::uint64_t kYieldInterval = 8192;

        while (gStopRequested == 0) {
            if (options.stepLimit.has_value() && steps >= *options.stepLimit) {
                break;
            }

            machine.step();
            ++steps;

            if (frontend != nullptr && (steps % kFrontendServiceInterval) == 0u) {
                frontend->serviceFrontend();
                if (frontend->quitRequested()) {
                    break;
                }
            }

            if ((steps % kYieldInterval) == 0u) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        if (frontend != nullptr) {
            frontend->serviceFrontend();
        }

        const auto pc = machine.runtimeContext().readRegister16(GB::RegisterId::PC);
        std::cout << "Stopped after " << steps
                  << " instruction steps at PC=0x"
                  << std::hex << std::uppercase << pc << std::dec << '\n';
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
