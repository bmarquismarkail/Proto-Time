#include "emulator/EmulatorHost.hpp"

#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace BMMQ {
namespace {

[[nodiscard]] std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path)
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

BootstrappedMachine::~BootstrappedMachine()
{
    if (captureStarted && machine != nullptr) {
        try {
            machine->visualOverrideService().endCapture();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "BootstrappedMachine::~BootstrappedMachine endCapture failed: %s\n", ex.what());
        } catch (...) {
            std::fputs("BootstrappedMachine::~BootstrappedMachine endCapture failed with unknown exception\n", stderr);
        }
        captureStarted = false;
    }
}

BootstrappedMachine bootstrapMachine(const EmulatorConfig& options)
{
    const auto kind = parseMachineKind(options.machineKind.value());
    auto instance = createMachine(kind);

    if (auto* romPathAware = dynamic_cast<IRomPathAwareMachine*>(instance.machine.get()); romPathAware != nullptr) {
        romPathAware->setRomSourcePath(options.romPath);
    }

    if (options.bootRomPath.has_value()) {
        auto* bootRomMachine = dynamic_cast<IExternalBootRomMachine*>(instance.machine.get());
        if (bootRomMachine == nullptr) {
            throw std::invalid_argument(
                std::string(instance.descriptor.displayName) + " does not support external boot ROM loading");
        }
        bootRomMachine->loadExternalBootRom(readBinaryFile(*options.bootRomPath));
    }

    const auto romBytes = readBinaryFile(options.romPath);
    instance.machine->loadRom(romBytes);

    if (!options.visualPackPaths.empty()) {
        if (!instance.machine->supportsVisualPacks()) {
            throw std::invalid_argument(
                std::string(instance.descriptor.displayName) + " does not support visual packs");
        }
        for (const auto& visualPackPath : options.visualPackPaths) {
            if (!instance.machine->visualOverrideService().loadPackManifest(visualPackPath)) {
                throw std::runtime_error(
                    "Unable to load visual pack: " + visualPackPath.string() +
                    " (" + instance.machine->visualOverrideService().lastError() + ")");
            }
        }
    }

    bool captureStarted = false;
    if (options.visualCapturePath.has_value()) {
        if (!instance.machine->supportsVisualCapture()) {
            throw std::invalid_argument(
                std::string(instance.descriptor.displayName) + " does not support visual capture");
        }
        if (!instance.machine->visualOverrideService().beginCapture(
                *options.visualCapturePath,
                std::string(instance.machine->visualTargetId()))) {
            throw std::runtime_error(
                "Unable to start visual resource capture: " + options.visualCapturePath->string() +
                " (" + instance.machine->visualOverrideService().lastError() + ")");
        }
        captureStarted = true;
    }

    BootstrappedMachine bootstrapped;
    bootstrapped.kind = instance.kind;
    bootstrapped.descriptor = instance.descriptor;
    bootstrapped.machine = std::move(instance.machine);
    bootstrapped.romSize = romBytes.size();
    bootstrapped.captureStarted = captureStarted;
    return bootstrapped;
}

} // namespace BMMQ
