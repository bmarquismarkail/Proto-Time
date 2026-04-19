#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
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
#include "machine/VisualOverrideService.hpp"
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
    auto replacedFrame = videoService.engine().buildDebugFrame(state, 2u);
    assert(replacedFrame.pixels[0] == 0xFFFF0000u);

    std::filesystem::remove_all(root);
    return 0;
}
