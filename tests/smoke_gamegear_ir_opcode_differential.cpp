#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "cores/gamegear/GameGearMachine.hpp"
#include "inst_cycle/executor/PluginContract.hpp"
#include "machine/SaveState.hpp"

namespace {

constexpr std::array<std::uint32_t, 4u> kSeeds{
    0x8ED5A9C3u, 0x1B74F082u, 0xD420C7F5u, 0x56EA3B01u,
};
constexpr std::array<std::uint8_t, 4u> kBoundaryBytes{0x00u, 0x7Fu, 0x80u, 0xFFu};
constexpr std::array<std::uint8_t, 4u> kFlagInputs{0x00u, 0x01u, 0xFEu, 0xFFu};
constexpr std::array<std::int8_t, 4u> kDisplacements{-128, -1, 0, 127};
constexpr std::array<std::uint16_t, 4u> kDataAddresses{
    0xC100u, 0xC17Fu, 0xD080u, 0xDFFFu,
};
constexpr std::uint16_t kStandardPc = 0x0200u;

struct CaseMetadata {
    std::uint32_t seed = 0u;
    std::uint8_t opcode = 0u;
    std::uint8_t variant = 0u;
    std::string description;
};

[[noreturn]] void fail(const char* expression, const CaseMetadata& meta)
{
    std::fprintf(stderr,
                 "Game Gear IR differential failure: seed=%08X opcode=%02X "
                 "case=%u (%s): %s\n",
                 meta.seed, meta.opcode, meta.variant,
                 meta.description.c_str(), expression);
    std::abort();
}

void check(bool condition, const char* expression, const CaseMetadata& meta)
{
    if (!condition) fail(expression, meta);
}

#define DIFF_CHECK(expression, metadata) \
    check(static_cast<bool>(expression), #expression, metadata)

std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return state;
}

std::vector<std::uint8_t> supportedOpcodes()
{
    std::vector<std::uint8_t> result{0x00u, 0x18u};
    for (std::uint16_t opcode = 0x40u; opcode <= 0x7Fu; ++opcode) {
        if (opcode != 0x76u) result.push_back(static_cast<std::uint8_t>(opcode));
    }
    for (std::uint8_t code = 0u; code < 8u; ++code) {
        result.push_back(static_cast<std::uint8_t>(0x04u + code * 8u));
        result.push_back(static_cast<std::uint8_t>(0x05u + code * 8u));
        result.push_back(static_cast<std::uint8_t>(0x06u + code * 8u));
    }
    for (std::uint16_t opcode = 0x80u; opcode <= 0xBFu; ++opcode) {
        result.push_back(static_cast<std::uint8_t>(opcode));
    }
    return result;
}

std::uint8_t instructionLength(std::uint8_t opcode) noexcept
{
    return opcode == 0x18u || (opcode & 0xC7u) == 0x06u ? 2u : 1u;
}

bool usesHlMemory(std::uint8_t opcode) noexcept
{
    if (opcode >= 0x40u && opcode <= 0x7Fu) {
        return (opcode & 0x07u) == 6u || ((opcode >> 3u) & 0x07u) == 6u;
    }
    if (opcode >= 0x80u && opcode <= 0xBFu) return (opcode & 0x07u) == 6u;
    return ((opcode & 0xC7u) == 0x04u ||
            (opcode & 0xC7u) == 0x05u ||
            (opcode & 0xC7u) == 0x06u) && ((opcode >> 3u) & 0x07u) == 6u;
}

void writeRegisterCode(BMMQ::GameGearMachine& machine,
                       std::uint8_t code,
                       std::uint8_t value)
{
    auto& context = machine.runtimeContext();
    if (code == 7u) {
        context.writeRegister8("A", value);
        return;
    }
    if (code == 6u) {
        context.write8(machine.readRegisterPair("HL"), value);
        return;
    }
    constexpr std::array<std::string_view, 3u> pairs{"BC", "DE", "HL"};
    const auto pairIndex = static_cast<std::size_t>(code / 2u);
    auto pair = machine.readRegisterPair(pairs[pairIndex]);
    if ((code & 1u) == 0u) {
        pair = static_cast<std::uint16_t>((pair & 0x00FFu) |
                                         (static_cast<std::uint16_t>(value) << 8u));
    } else {
        pair = static_cast<std::uint16_t>((pair & 0xFF00u) | value);
    }
    context.writeRegister16(pairs[pairIndex], pair);
}

std::vector<std::uint8_t> makeRom(const CaseMetadata& meta,
                                  std::uint16_t pc = kStandardPc)
{
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    rom[pc] = meta.opcode;
    if (instructionLength(meta.opcode) == 2u) {
        rom[static_cast<std::uint16_t>(pc + 1u)] =
            meta.opcode == 0x18u
                ? static_cast<std::uint8_t>(kDisplacements[meta.variant])
                : kBoundaryBytes[meta.variant];
    }
    return rom;
}

void initializeMachine(BMMQ::GameGearMachine& machine,
                       const CaseMetadata& meta,
                       std::uint16_t pc = kStandardPc)
{
    auto random = meta.seed ^ (static_cast<std::uint32_t>(meta.opcode) << 8u) ^
                  meta.variant;
    auto& context = machine.runtimeContext();
    for (const std::string_view pair : {"AF", "BC", "DE", "HL", "IX", "IY"}) {
        context.writeRegister16(pair,
            static_cast<std::uint16_t>(nextRandom(random)));
    }
    context.writeRegister16("SP",
        std::array<std::uint16_t, 4u>{0x0000u, 0x0001u, 0xC000u, 0xFFFFu}[meta.variant]);
    context.writeRegister16("PC", pc);
    context.writeRegister8("F", kFlagInputs[meta.variant]);
    context.write8(0xC000u, static_cast<std::uint8_t>(nextRandom(random)));
    context.write8(0xDFFEu, static_cast<std::uint8_t>(nextRandom(random)));

    const auto opcode = meta.opcode;
    const bool memoryForm = usesHlMemory(opcode);
    if (memoryForm) context.writeRegister16("HL", kDataAddresses[meta.variant]);

    if (opcode >= 0x40u && opcode <= 0x7Fu) {
        const auto source = static_cast<std::uint8_t>(opcode & 0x07u);
        const auto destination = static_cast<std::uint8_t>((opcode >> 3u) & 0x07u);
        if (!memoryForm || (source != 4u && source != 5u)) {
            writeRegisterCode(machine, source, kBoundaryBytes[meta.variant]);
        }
        if (destination != source && destination != 6u &&
            (!memoryForm || (destination != 4u && destination != 5u))) {
            writeRegisterCode(machine, destination,
                              static_cast<std::uint8_t>(~kBoundaryBytes[meta.variant]));
        }
    } else if ((opcode & 0xC7u) == 0x04u || (opcode & 0xC7u) == 0x05u) {
        writeRegisterCode(machine, static_cast<std::uint8_t>((opcode >> 3u) & 7u),
                          kBoundaryBytes[meta.variant]);
    } else if ((opcode & 0xC7u) == 0x06u) {
        writeRegisterCode(machine, static_cast<std::uint8_t>((opcode >> 3u) & 7u),
                          static_cast<std::uint8_t>(~kBoundaryBytes[meta.variant]));
    } else if (opcode >= 0x80u && opcode <= 0xBFu) {
        context.writeRegister8("A", kBoundaryBytes[(meta.variant + 1u) % 4u]);
        const auto source = static_cast<std::uint8_t>(opcode & 7u);
        if (!memoryForm || (source != 4u && source != 5u)) {
            writeRegisterCode(machine, source, kBoundaryBytes[meta.variant]);
        }
    }
}

void assertEquivalent(const CaseMetadata& meta,
                      const BMMQ::GameGearMachine& baseline,
                      const BMMQ::GameGearMachine& ir)
{
    for (const std::string_view pair : {
             "AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC"}) {
        DIFF_CHECK(baseline.readRegisterPair(pair) == ir.readRegisterPair(pair), meta);
    }
    for (std::uint32_t address = 0xC000u; address < 0xE000u; ++address) {
        DIFF_CHECK(baseline.runtimeContext().read8(static_cast<std::uint16_t>(address)) ==
                   ir.runtimeContext().read8(static_cast<std::uint16_t>(address)), meta);
    }
    const auto& expected = baseline.runtimeContext().getLastFeedback();
    const auto& actual = ir.runtimeContext().getLastFeedback();
    DIFF_CHECK(expected.pcBefore == actual.pcBefore, meta);
    DIFF_CHECK(expected.pcAfter == actual.pcAfter, meta);
    DIFF_CHECK(expected.retiredCycles == actual.retiredCycles, meta);
    DIFF_CHECK(expected.isControlFlow == actual.isControlFlow, meta);
    DIFF_CHECK(expected.segmentBoundaryHint == actual.segmentBoundaryHint, meta);
    DIFF_CHECK(baseline.recentAudioSamples() == ir.recentAudioSamples(), meta);
    DIFF_CHECK(baseline.audioFrameCounter() == ir.audioFrameCounter(), meta);

    const auto expectedVideo = baseline.videoDebugFrameModel({16, 16});
    const auto actualVideo = ir.videoDebugFrameModel({16, 16});
    DIFF_CHECK(expectedVideo.has_value() == actualVideo.has_value(), meta);
    if (expectedVideo.has_value()) {
        DIFF_CHECK(expectedVideo->width == actualVideo->width, meta);
        DIFF_CHECK(expectedVideo->height == actualVideo->height, meta);
        DIFF_CHECK(expectedVideo->displayEnabled == actualVideo->displayEnabled, meta);
        DIFF_CHECK(expectedVideo->inVBlank == actualVideo->inVBlank, meta);
        DIFF_CHECK(expectedVideo->scanlineIndex == actualVideo->scanlineIndex, meta);
        DIFF_CHECK(expectedVideo->argbPixels == actualVideo->argbPixels, meta);
    }
}

void executeAndCompare(const CaseMetadata& meta)
{
    const auto rom = makeRom(meta);
    BMMQ::GameGearMachine baseline;
    BMMQ::GameGearMachine ir;
    baseline.loadRom(rom);
    ir.loadRom(rom);
    initializeMachine(baseline, meta);
    initializeMachine(ir, meta);
    BMMQ::Plugin::PortableIrStepPolicy policy;
    ir.attachExecutorPolicy(policy);

    baseline.step();
    ir.step();
    DIFF_CHECK(baseline.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::CanonicalFetchDecodeExecute, meta);
    DIFF_CHECK(ir.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::PortableIr, meta);
    assertEquivalent(meta, baseline, ir);
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto pattern = (std::filesystem::temp_directory_path() /
                              "proto-time-gg-ir-differential-XXXXXX").string();
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const auto* created = mkdtemp(writable.data());
        if (created == nullptr)
            throw std::runtime_error("unable to create differential temp directory");
        path_ = created;
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void assertSerializedStateEqual(const CaseMetadata& meta,
                                BMMQ::GameGearMachine& baseline,
                                BMMQ::GameGearMachine& ir,
                                const TemporaryDirectory& temporary,
                                std::string_view suffix)
{
    const auto baselinePath = temporary.path() / ("baseline-" + std::string(suffix) + ".ptss");
    const auto irPath = temporary.path() / ("ir-" + std::string(suffix) + ".ptss");
    baseline.save_state(baselinePath);
    ir.save_state(irPath);
    DIFF_CHECK(readFile(baselinePath) == readFile(irPath), meta);
}

void testExecutionStateFallbacks()
{
    TemporaryDirectory temporary;
    for (const auto [prelude, name] :
         std::array<std::pair<std::uint8_t, const char*>, 2u>{
             std::pair{0x76u, "halted"}, std::pair{0xFBu, "deferred EI"}}) {
        CaseMetadata meta{0x11223344u, 0x04u, prelude, name};
        std::vector<std::uint8_t> rom(0x8000u, 0x00u);
        rom[0] = prelude;
        rom[1] = meta.opcode;
        BMMQ::GameGearMachine baseline;
        BMMQ::GameGearMachine ir;
        baseline.loadRom(rom);
        ir.loadRom(rom);
        BMMQ::Plugin::PortableIrStepPolicy policy;
        ir.attachExecutorPolicy(policy);
        baseline.step();
        ir.step();
        baseline.step();
        ir.step();
        DIFF_CHECK(ir.runtimeContext().getLastFeedback().executionPath ==
                   BMMQ::ExecutionPathHint::CanonicalFetchDecodeExecute, meta);
        assertEquivalent(meta, baseline, ir);
        assertSerializedStateEqual(meta, baseline, ir, temporary, name);
    }

    CaseMetadata pending{0x55667788u, 0x00u, 0u, "interrupt pending"};
    const auto rom = makeRom(pending, 0u);
    BMMQ::GameGearMachine baseline;
    BMMQ::GameGearMachine ir;
    baseline.loadRom(rom);
    ir.loadRom(rom);
    BMMQ::Plugin::PortableIrStepPolicy policy;
    ir.attachExecutorPolicy(policy);
    for (auto* machine : {&baseline, &ir}) {
        const auto statePath = temporary.path() /
            (machine == &baseline ? "pending-baseline.ptss" : "pending-ir.ptss");
        machine->save_state(statePath);
        auto state = BMMQ::SaveStateReader::read(statePath);
        for (auto& chunk : state.chunks) {
            if (chunk.name == "gg.machine") chunk.data.at(16u) = 1u;
        }
        BMMQ::SaveStateReader::write(state, statePath);
        machine->load_state(statePath);
    }
    baseline.step();
    ir.step();
    DIFF_CHECK(ir.runtimeContext().getLastFeedback().executionPath ==
               BMMQ::ExecutionPathHint::CanonicalFetchDecodeExecute, pending);
    assertEquivalent(pending, baseline, ir);
    assertSerializedStateEqual(pending, baseline, ir, temporary, "pending-final");
}

void testAddressBoundaries()
{
    struct BoundaryCase {
        std::uint16_t pc;
        std::array<std::uint8_t, 2u> bytes;
        std::uint8_t length;
        bool expectIr;
        const char* name;
    };
    constexpr std::array cases{
        BoundaryCase{0xFDFFu, {0x00u, 0x00u}, 1u, true, "last safe byte"},
        BoundaryCase{0xFFFFu, {0x00u, 0x00u}, 1u, false, "PC wrap fallback"},
        BoundaryCase{0xFDFFu, {0x06u, 0x42u}, 2u, false, "unsafe immediate"},
        BoundaryCase{0xFDFDu, {0x18u, 0x7Fu}, 2u, true, "branch to unsafe window"},
    };
    for (std::uint8_t index = 0u; index < cases.size(); ++index) {
        const auto& boundary = cases[index];
        CaseMetadata meta{0xA5A5A5A5u, boundary.bytes[0], index, boundary.name};
        std::vector<std::uint8_t> rom(0x8000u, 0x00u);
        BMMQ::GameGearMachine baseline;
        BMMQ::GameGearMachine ir;
        baseline.loadRom(rom);
        ir.loadRom(rom);
        for (auto* machine : {&baseline, &ir}) {
            for (std::uint8_t offset = 0u; offset < boundary.length; ++offset) {
                machine->runtimeContext().write8(
                    static_cast<std::uint16_t>(boundary.pc + offset), boundary.bytes[offset]);
            }
            machine->runtimeContext().writeRegister16("PC", boundary.pc);
            machine->runtimeContext().writeRegister16("BC", 0x1234u);
        }
        BMMQ::Plugin::PortableIrStepPolicy policy;
        ir.attachExecutorPolicy(policy);
        baseline.step();
        ir.step();
        DIFF_CHECK((ir.runtimeContext().getLastFeedback().executionPath ==
                    BMMQ::ExecutionPathHint::PortableIr) == boundary.expectIr, meta);
        assertEquivalent(meta, baseline, ir);
    }
}

void testCacheReuseAndInvalidation()
{
    CaseMetadata meta{0xDEADBEEFu, 0x00u, 0u, "cache reuse and invalidation"};
    std::vector<std::uint8_t> rom(0x8000u, 0x00u);
    BMMQ::GameGearMachine baseline;
    BMMQ::GameGearMachine ir;
    baseline.loadRom(rom);
    ir.loadRom(rom);
    BMMQ::Plugin::PortableIrStepPolicy policy;
    ir.attachExecutorPolicy(policy);
    for (auto* machine : {&baseline, &ir}) {
        machine->runtimeContext().write8(0xC000u, 0x00u);
        machine->runtimeContext().writeRegister16("PC", 0xC000u);
        machine->runtimeContext().writeRegister16("BC", 0x7F00u);
    }
    baseline.step();
    ir.step();
    const auto first = ir.irStats();
    assertEquivalent(meta, baseline, ir);

    for (auto* machine : {&baseline, &ir}) {
        machine->runtimeContext().writeRegister16("PC", 0xC000u);
    }
    baseline.step();
    ir.step();
    const auto reused = ir.irStats();
    DIFF_CHECK(reused.translations == first.translations, meta);
    assertEquivalent(meta, baseline, ir);

    for (auto* machine : {&baseline, &ir}) {
        machine->runtimeContext().write8(0xC100u, 0xA5u);
        machine->runtimeContext().writeRegister16("PC", 0xC000u);
    }
    baseline.step();
    ir.step();
    const auto unrelated = ir.irStats();
    DIFF_CHECK(unrelated.translations == reused.translations, meta);
    assertEquivalent(meta, baseline, ir);

    for (auto* machine : {&baseline, &ir}) {
        machine->runtimeContext().write8(0xC000u, 0x04u);
        machine->runtimeContext().writeRegister16("PC", 0xC000u);
    }
    baseline.step();
    ir.step();
    const auto changed = ir.irStats();
    DIFF_CHECK(changed.translations == unrelated.translations + 1u, meta);
    assertEquivalent(meta, baseline, ir);
}

} // namespace

int main()
{
    for (const auto opcode : supportedOpcodes()) {
        for (std::uint8_t variant = 0u; variant < kSeeds.size(); ++variant) {
            CaseMetadata meta{kSeeds[variant], opcode, variant, "supported opcode"};
            executeAndCompare(meta);
        }
    }
    testExecutionStateFallbacks();
    testAddressBoundaries();
    testCacheReuseAndInvalidation();
    return 0;
}
