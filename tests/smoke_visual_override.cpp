#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VisualCaptureWriter.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualPackLimits.hpp"
#include "machine/VisualTypes.hpp"

namespace {

void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << contents;
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
}

std::vector<uint8_t> makePng2x2Rgba()
{
    return {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D,
        0x24, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
        0x1F, 0x0C, 0x81, 0x34, 0x18, 0x00, 0x00, 0x49,
        0xC8, 0x09, 0xF7, 0xF9, 0xAB, 0xB6, 0x0D, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82,
    };
}

std::vector<uint8_t> makeHeaderOnlyPng(uint32_t width, uint32_t height)
{
    return {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        static_cast<uint8_t>((width >> 24u) & 0xFFu),
        static_cast<uint8_t>((width >> 16u) & 0xFFu),
        static_cast<uint8_t>((width >> 8u) & 0xFFu),
        static_cast<uint8_t>(width & 0xFFu),
        static_cast<uint8_t>((height >> 24u) & 0xFFu),
        static_cast<uint8_t>((height >> 16u) & 0xFFu),
        static_cast<uint8_t>((height >> 8u) & 0xFFu),
        static_cast<uint8_t>(height & 0xFFu),
        0x08, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
        0x00, 0x00, 0x00, 0x00,
    };
}

BMMQ::VideoStateView makeTileState(uint8_t lowByte, uint8_t highByte)
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x91u;
    state.bgp = 0xE4u;
    for (std::size_t row = 0; row < 8u; ++row) {
        state.vram[row * 2u] = lowByte;
        state.vram[row * 2u + 1u] = highByte;
    }
    state.vram[0x1800u] = 0x00u;
    return state;
}

void writeSingleRulePack(const std::filesystem::path& manifestPath,
                         std::string_view id,
                         BMMQ::VisualResourceKind kind,
                         BMMQ::VisualResourceHash hash,
                         const std::filesystem::path& imagePath)
{
    writeBinaryFile(imagePath, makePng2x2Rgba());
    writeTextFile(manifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"" + std::string(id) + "\",\n"
        "  \"name\": \"Smoke Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"" + BMMQ::visualResourceKindName(kind) + "\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(hash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"" + imagePath.filename().string() + "\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "bmmq_visual_override_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto state = makeTileState(0xFFu, 0x00u);
    auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
    assert(resource.has_value());
    assert(resource->descriptor.machineId == "gameboy");
    assert(resource->descriptor.kind == BMMQ::VisualResourceKind::Tile);
    assert(resource->descriptor.width == 8u);
    assert(resource->descriptor.height == 8u);
    assert(resource->descriptor.decodedFormat == BMMQ::VisualPixelFormat::Indexed2);
    assert(resource->descriptor.contentHash != 0u);
    assert(resource->descriptor.paletteHash != 0u);
    assert(resource->descriptor.paletteAwareHash != 0u);
    assert(resource->descriptor.paletteAwareHash != resource->descriptor.contentHash);
    assert(resource->pixels.size() == 64u);

    auto identical = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
    assert(identical.has_value());
    assert(identical->descriptor.contentHash == resource->descriptor.contentHash);
    assert(identical->descriptor.paletteHash == resource->descriptor.paletteHash);
    assert(identical->descriptor.paletteAwareHash == resource->descriptor.paletteAwareHash);

    auto differentState = makeTileState(0x00u, 0xFFu);
    auto different = GB::decodeGameBoyTileResource(differentState, 0u, BMMQ::VisualResourceKind::Tile);
    assert(different.has_value());
    assert(different->descriptor.contentHash != resource->descriptor.contentHash);

    auto differentPaletteState = state;
    differentPaletteState.bgp = 0x1Bu;
    auto differentPalette = GB::decodeGameBoyTileResource(differentPaletteState, 0u, BMMQ::VisualResourceKind::Tile);
    assert(differentPalette.has_value());
    assert(differentPalette->descriptor.contentHash == resource->descriptor.contentHash);
    assert(differentPalette->descriptor.paletteHash != resource->descriptor.paletteHash);
    assert(differentPalette->descriptor.paletteAwareHash != resource->descriptor.paletteAwareHash);

    const std::string paletteRegisterText = "OBP1";
    const std::string_view paletteRegisterView = paletteRegisterText;
    auto explicitPaletteRegister =
        GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Sprite, 0xAAu, paletteRegisterView);
    assert(explicitPaletteRegister.has_value());
    assert(explicitPaletteRegister->descriptor.source.paletteValue == 0xAAu);
    assert(explicitPaletteRegister->descriptor.source.paletteRegister == "OBP1");
    auto escapedMetadataResource = *explicitPaletteRegister;
    escapedMetadataResource.descriptor.source.paletteRegister = std::string("A") + '\b' + '\f' + '\x01' + 'Z';

    BMMQ::VisualOverrideService service;
    assert(service.enabled());
    const auto imagePath = root / "pack" / "images" / "tile.png";
    writeBinaryFile(imagePath, makePng2x2Rgba());
    const auto manifestPath = root / "pack" / "pack.json";
    writeTextFile(manifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"smoke.gb\",\n"
        "  \"name\": \"Smoke Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/tile.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");

    assert(service.loadPackManifest(manifestPath));
    assert(service.diagnostics().rulesLoaded == 1u);
    auto resolved = service.resolve(resource->descriptor);
    assert(resolved.has_value());
    assert(resolved->image.width == 2u);
    assert(resolved->image.height == 2u);
    assert(resolved->image.argbPixels.size() == 4u);
    assert(resolved->image.argbPixels[0] == 0xFFFF0000u);
    assert(service.diagnostics().resolveHits == 1u);

    const auto semanticImagePath = root / "semantic-pack" / "images" / "tile.png";
    writeBinaryFile(semanticImagePath, makePng2x2Rgba());
    const auto semanticManifestPath = root / "semantic-pack" / "pack.json";
    writeTextFile(semanticManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"semantic.gb\",\n"
        "  \"name\": \"Semantic Pack\",\n"
        "  \"target\": \"gameboy\",\n"
        "  \"priority\": 3,\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"machineId\": \"gameboy\",\n"
        "        \"decodedFormat\": \"Indexed2\",\n"
        "        \"semanticLabel\": \"hud_digit\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/tile.png\",\n"
        "        \"scalePolicy\": \"nearest\",\n"
        "        \"filterPolicy\": \"preserve-hard-edges\",\n"
        "        \"anchor\": \"top-left\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService semanticService;
    assert(semanticService.loadPackManifest(semanticManifestPath));
    assert(!semanticService.resolve(resource->descriptor).has_value());
    auto labeledDescriptor = resource->descriptor;
    labeledDescriptor.source.label = "hud_digit";
    auto semanticResolved = semanticService.resolve(labeledDescriptor);
    assert(semanticResolved.has_value());
    assert(semanticResolved->packId == "semantic.gb");
    assert(semanticResolved->scalePolicy == "nearest");
    assert(semanticResolved->filterPolicy == "preserve-hard-edges");
    assert(semanticResolved->anchor == "top-left");

    const auto lowPriorityImagePath = root / "priority-low-pack" / "tile.png";
    const auto highPriorityImagePath = root / "priority-high-pack" / "tile.png";
    writeBinaryFile(lowPriorityImagePath, makePng2x2Rgba());
    writeBinaryFile(highPriorityImagePath, makePng2x2Rgba());
    writeTextFile(root / "priority-low-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"priority-low.gb\",\n"
        "  \"target\": \"gameboy\",\n"
        "  \"priority\": 1,\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"tile.png\"}\n"
        "  }]\n"
        "}\n");
    writeTextFile(root / "priority-high-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"priority-high.gb\",\n"
        "  \"target\": \"gameboy\",\n"
        "  \"priority\": 9,\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService priorityService;
    assert(priorityService.loadPackManifest(root / "priority-low-pack" / "pack.json"));
    assert(priorityService.loadPackManifest(root / "priority-high-pack" / "pack.json"));
    auto priorityResolved = priorityService.resolve(resource->descriptor);
    assert(priorityResolved.has_value());
    assert(priorityResolved->packId == "priority-high.gb");

    const auto paletteImagePath = root / "palette-pack" / "images" / "tile.png";
    writeBinaryFile(paletteImagePath, makePng2x2Rgba());
    const auto paletteManifestPath = root / "palette-pack" / "pack.json";
    writeTextFile(paletteManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"palette.gb\",\n"
        "  \"name\": \"Palette Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"paletteAwareHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.paletteAwareHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/tile.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService paletteService;
    assert(paletteService.loadPackManifest(paletteManifestPath));
    assert(paletteService.resolve(resource->descriptor).has_value());
    assert(!paletteService.resolve(differentPalette->descriptor).has_value());
    assert(paletteService.diagnostics().resolveMisses == 1u);

    const auto paletteHashImagePath = root / "palette-hash-pack" / "images" / "tile.png";
    writeBinaryFile(paletteHashImagePath, makePng2x2Rgba());
    const auto paletteHashManifestPath = root / "palette-hash-pack" / "pack.json";
    writeTextFile(paletteHashManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"palette-hash.gb\",\n"
        "  \"name\": \"Palette Hash Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"paletteHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.paletteHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/tile.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService paletteHashService;
    assert(paletteHashService.loadPackManifest(paletteHashManifestPath));
    assert(paletteHashService.resolve(resource->descriptor).has_value());
    assert(!paletteHashService.resolve(differentPalette->descriptor).has_value());
    assert(paletteHashService.diagnostics().resolveMisses == 1u);

    service.setEnabled(false);
    assert(!service.resolve(resource->descriptor).has_value());
    service.setEnabled(true);

    writeTextFile(root / "pack" / "bad.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"bad.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/missing.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService missingAssetService;
    assert(missingAssetService.loadPackManifest(root / "pack" / "bad.json"));
    assert(!missingAssetService.resolve(resource->descriptor).has_value());
    assert(missingAssetService.diagnostics().missingReplacementImages == 1u);

    writeTextFile(root / "pack" / "invalid-rules.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"invalid-rules.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {}\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService invalidRuleService;
    assert(invalidRuleService.loadPackManifest(root / "pack" / "invalid-rules.json"));
    assert(invalidRuleService.diagnostics().invalidRulesSkipped == 1u);
    assert(invalidRuleService.diagnostics().rulesLoaded == 0u);

    BMMQ::VisualOverrideService captureService;
    const auto dumpDir = root / "capture";
    assert(captureService.beginCapture(dumpDir, "gameboy"));
    assert(captureService.observe(*resource));
    assert(captureService.observe(escapedMetadataResource));
    assert(!captureService.observe(*identical));
    assert(captureService.captureStats().uniqueResourcesDumped == 2u);
    const auto captureManifest = dumpDir / "manifest.stub.json";
    assert(std::filesystem::exists(captureManifest));
    const auto capturePack = dumpDir / "pack.json";
    assert(std::filesystem::exists(capturePack));
    const auto captureMetadata = dumpDir / "capture_metadata.json";
    assert(std::filesystem::exists(captureMetadata));
    const auto captureManifestText = readTextFile(captureManifest);
    assert(captureManifestText.find("\"paletteHash\"") != std::string::npos);
    assert(captureManifestText.find("\"paletteAwareHash\"") != std::string::npos);
    const auto capturePackText = readTextFile(capturePack);
    assert(capturePackText.find("\"name\": \"Captured gameboy visual resources\"") != std::string::npos);
    assert(capturePackText.find("\"metadata\"") == std::string::npos);
    const auto captureMetadataText = readTextFile(captureMetadata);
    assert(captureMetadataText.find("\"sourceAddress\"") != std::string::npos);
    assert(captureMetadataText.find("\"paletteRegister\": \"BGP\"") != std::string::npos);
    assert(captureMetadataText.find("\"paletteRegister\": \"A\\b\\f\\u0001Z\"") != std::string::npos);

    BMMQ::VisualOverrideService capturedPackService;
    assert(capturedPackService.loadPackManifest(capturePack));
    assert(capturedPackService.resolve(resource->descriptor).has_value());

    BMMQ::DecodedVisualResource invalidCaptureResource = *resource;
    invalidCaptureResource.descriptor.width = 0u;
    invalidCaptureResource.descriptor.height = 8u;
    invalidCaptureResource.stride = 0u;
    std::string captureWriteError = "unchanged";
    assert(!BMMQ::VisualCaptureWriter::writeDecodedResourcePng(root / "invalid.png",
                                                               invalidCaptureResource,
                                                               captureWriteError));
    assert(captureWriteError.find("width=0") != std::string::npos);
    assert(captureWriteError.find("height=8") != std::string::npos);
    assert(captureWriteError.find("stride=0") != std::string::npos);

    const auto unicodeImagePath = root / "unicode-pack" / "tile.png";
    writeBinaryFile(unicodeImagePath, makePng2x2Rgba());
    const auto unicodeManifestPath = root / "unicode-pack" / "pack.json";
    writeTextFile(unicodeManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"unicode.\\u0041.\\uD83D\\uDE00\",\n"
        "  \"name\": \"Unicode Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"tile.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService unicodePackService;
    assert(unicodePackService.loadPackManifest(unicodeManifestPath));
    auto unicodeResolved = unicodePackService.resolve(resource->descriptor);
    assert(unicodeResolved.has_value());
    assert(unicodeResolved->packId == std::string("unicode.A.") + "\xF0\x9F\x98\x80");

    writeTextFile(root / "pack" / "garbage-hash.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"garbage-hash.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "garbage\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/tile.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService garbageHashService;
    assert(garbageHashService.loadPackManifest(root / "pack" / "garbage-hash.json"));
    assert(garbageHashService.diagnostics().invalidRulesSkipped == 1u);
    assert(garbageHashService.diagnostics().rulesLoaded == 0u);

    writeTextFile(root / "oversized-pack" / "pack.json",
                  std::string(1024u * 1024u + 1u, ' '));
    BMMQ::VisualOverrideService oversizedManifestService;
    assert(!oversizedManifestService.loadPackManifest(root / "oversized-pack" / "pack.json"));
    assert(oversizedManifestService.lastError().find("visual pack manifest too large") != std::string::npos);

    std::string tooManyRules =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"too-many-rules.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n";
    for (std::size_t i = 0; i < 1025u; ++i) {
        tooManyRules +=
            "    {\n"
            "      \"match\": {\n"
            "        \"kind\": \"Tile\",\n"
            "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
            "        \"width\": 8,\n"
            "        \"height\": 8\n"
            "      },\n"
            "      \"replace\": {\n"
            "        \"image\": \"images/tile.png\"\n"
            "      }\n"
            "    }" + std::string(i + 1u < 1025u ? "," : "") + "\n";
    }
    tooManyRules += "  ]\n}\n";
    writeTextFile(root / "too-many-rules" / "pack.json", tooManyRules);
    BMMQ::VisualOverrideService tooManyRulesService;
    assert(!tooManyRulesService.loadPackManifest(root / "too-many-rules" / "pack.json"));
    assert(tooManyRulesService.lastError().find("too many visual pack rules") != std::string::npos);

    const auto oversizedImagePath = root / "oversized-image-pack" / "large.png";
    writeBinaryFile(oversizedImagePath,
                    makeHeaderOnlyPng(BMMQ::VisualPackLimits::kMaxReplacementImageDimension + 1u, 1u));
    writeTextFile(root / "oversized-image-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"oversized-image.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"large.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService oversizedImageService;
    assert(oversizedImageService.loadPackManifest(root / "oversized-image-pack" / "pack.json"));
    assert(!oversizedImageService.resolve(resource->descriptor).has_value());
    assert(oversizedImageService.diagnostics().replacementLoadFailures == 1u);

    const auto oversizedInflateImagePath = root / "oversized-inflate-pack" / "large.png";
    writeBinaryFile(oversizedInflateImagePath, makeHeaderOnlyPng(2048u, 2048u));
    writeTextFile(root / "oversized-inflate-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"oversized-inflate.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"large.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService oversizedInflateService;
    assert(oversizedInflateService.loadPackManifest(root / "oversized-inflate-pack" / "pack.json"));
    assert(!oversizedInflateService.resolve(resource->descriptor).has_value());
    assert(oversizedInflateService.diagnostics().replacementLoadFailures == 1u);

    GameBoyMachine machine;
    assert(&machine.visualOverrideService() == &machine.mutableView().visualOverrideService());
    assert(machine.setVisualOverrideService(std::make_unique<BMMQ::VisualOverrideService>()));
    auto* swappedService = &machine.visualOverrideService();
    machine.pluginManager().initialize(machine.mutableView());
    assert(!machine.setVisualOverrideService(std::make_unique<BMMQ::VisualOverrideService>()));
    assert(&machine.visualOverrideService() == swappedService);
    machine.pluginManager().shutdown(machine.mutableView());

    BMMQ::VideoService videoService(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .queueCapacityFrames = 1,
    });
    auto originalFrame = videoService.engine().buildDebugFrame(state, 1u);
    assert(originalFrame.pixels[0] == 0xFF88C070u);

    videoService.setVisualOverrideService(&service);
    std::vector<BMMQ::MachineEventType> visualEvents;
    service.setEventSink([&visualEvents](const BMMQ::MachineEvent& event) {
        assert(event.category == BMMQ::PluginCategory::Video);
        visualEvents.push_back(event.type);
    });
    auto replacedFrame = videoService.engine().buildDebugFrame(state, 2u);
    assert(replacedFrame.pixels[0] == 0xFFFF0000u);
    assert(!visualEvents.empty());
    assert(visualEvents.front() == BMMQ::MachineEventType::FrameCompositionStarted);
    assert(visualEvents.back() == BMMQ::MachineEventType::FrameCompositionCompleted);
    assert(std::find(visualEvents.begin(),
                     visualEvents.end(),
                     BMMQ::MachineEventType::VisualResourceObserved) != visualEvents.end());
    assert(std::find(visualEvents.begin(),
                     visualEvents.end(),
                     BMMQ::MachineEventType::VisualResourceDecoded) != visualEvents.end());
    assert(std::find(visualEvents.begin(),
                     visualEvents.end(),
                     BMMQ::MachineEventType::VisualOverrideResolved) != visualEvents.end());

    auto signedTileState = makeTileState(0x00u, 0x00u);
    signedTileState.lcdc = 0x81u;
    for (std::size_t row = 0; row < 8u; ++row) {
        signedTileState.vram[0x1000u + row * 2u] = 0xFFu;
        signedTileState.vram[0x1000u + row * 2u + 1u] = 0x00u;
    }
    signedTileState.vram[0x1800u] = 0x00u;
    auto signedTileResource = GB::decodeGameBoyTileResource(signedTileState, 0x100u, BMMQ::VisualResourceKind::Tile);
    assert(signedTileResource.has_value());
    BMMQ::VisualOverrideService signedTileService;
    writeSingleRulePack(root / "signed-pack" / "pack.json",
                        "signed.gb",
                        BMMQ::VisualResourceKind::Tile,
                        signedTileResource->descriptor.contentHash,
                        root / "signed-pack" / "signed.png");
    assert(signedTileService.loadPackManifest(root / "signed-pack" / "pack.json"));
    BMMQ::VideoService signedTileVideoService(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .queueCapacityFrames = 1,
    });
    signedTileVideoService.setVisualOverrideService(&signedTileService);
    auto signedTileFrame = signedTileVideoService.engine().buildDebugFrame(signedTileState, 3u);
    assert(signedTileFrame.pixels[0] == 0xFFFF0000u);

    auto transparentSpriteState = makeTileState(0x00u, 0x00u);
    transparentSpriteState.lcdc = 0x93u;
    transparentSpriteState.oam[0] = 16u;
    transparentSpriteState.oam[1] = 8u;
    transparentSpriteState.oam[2] = 0x00u;
    transparentSpriteState.oam[3] = 0x00u;
    auto transparentSpriteResource =
        GB::decodeGameBoyTileResource(transparentSpriteState, 0u, BMMQ::VisualResourceKind::Sprite);
    assert(transparentSpriteResource.has_value());
    BMMQ::VisualOverrideService transparentSpriteService;
    writeSingleRulePack(root / "transparent-sprite-pack" / "pack.json",
                        "transparent-sprite.gb",
                        BMMQ::VisualResourceKind::Sprite,
                        transparentSpriteResource->descriptor.contentHash,
                        root / "transparent-sprite-pack" / "sprite.png");
    assert(transparentSpriteService.loadPackManifest(root / "transparent-sprite-pack" / "pack.json"));
    BMMQ::VideoService transparentSpriteVideoService(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .queueCapacityFrames = 1,
    });
    transparentSpriteVideoService.setVisualOverrideService(&transparentSpriteService);
    auto transparentSpriteFrame = transparentSpriteVideoService.engine().buildDebugFrame(transparentSpriteState, 4u);
    assert(transparentSpriteFrame.pixels[0] == 0xFFE0F8D0u);

    auto prioritySpriteState = makeTileState(0xFFu, 0x00u);
    prioritySpriteState.lcdc = 0x93u;
    for (std::size_t row = 0; row < 8u; ++row) {
        prioritySpriteState.vram[0x0010u + row * 2u] = 0xFFu;
        prioritySpriteState.vram[0x0010u + row * 2u + 1u] = 0xFFu;
    }
    prioritySpriteState.oam[0] = 16u;
    prioritySpriteState.oam[1] = 8u;
    prioritySpriteState.oam[2] = 0x01u;
    prioritySpriteState.oam[3] = 0x80u;
    auto prioritySpriteResource =
        GB::decodeGameBoyTileResource(prioritySpriteState, 1u, BMMQ::VisualResourceKind::Sprite);
    assert(prioritySpriteResource.has_value());
    BMMQ::VisualOverrideService prioritySpriteService;
    writeSingleRulePack(root / "priority-sprite-pack" / "pack.json",
                        "priority-sprite.gb",
                        BMMQ::VisualResourceKind::Sprite,
                        prioritySpriteResource->descriptor.contentHash,
                        root / "priority-sprite-pack" / "sprite.png");
    assert(prioritySpriteService.loadPackManifest(root / "priority-sprite-pack" / "pack.json"));
    BMMQ::VideoService prioritySpriteVideoService(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .queueCapacityFrames = 1,
    });
    prioritySpriteVideoService.setVisualOverrideService(&prioritySpriteService);
    auto prioritySpriteFrame = prioritySpriteVideoService.engine().buildDebugFrame(prioritySpriteState, 5u);
    assert(prioritySpriteFrame.pixels[0] == 0xFF88C070u);

    std::filesystem::remove_all(root);
    return 0;
}
