#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <filesystem>
#include <string>
#include <string_view>

#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VisualCaptureWriter.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    const auto root = std::filesystem::temp_directory_path() / "bmmq_visual_capture_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto state = Visual::makeTileState(0xFFu, 0x00u);
    auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
    assert(resource.has_value());

    auto identical = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
    assert(identical.has_value());

    const std::string paletteRegisterText = "OBP1";
    const std::string_view paletteRegisterView = paletteRegisterText;
    auto explicitPaletteRegister =
        GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Sprite, 0xAAu, paletteRegisterView);
    assert(explicitPaletteRegister.has_value());
    assert(explicitPaletteRegister->descriptor.source.paletteValue == 0xAAu);
    assert(explicitPaletteRegister->descriptor.source.paletteRegister == "OBP1");

    auto escapedMetadataResource = *explicitPaletteRegister;
    escapedMetadataResource.descriptor.source.paletteRegister = std::string("A") + '\b' + '\f' + '\x01' + 'Z';

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

    const auto captureManifestText = Visual::readTextFile(captureManifest);
    assert(captureManifestText.find("\"paletteHash\"") != std::string::npos);
    assert(captureManifestText.find("\"paletteAwareHash\"") != std::string::npos);
    const auto capturePackText = Visual::readTextFile(capturePack);
    assert(capturePackText.find("\"name\": \"Captured gameboy visual resources\"") != std::string::npos);
    assert(capturePackText.find("\"metadata\"") == std::string::npos);
    const auto captureMetadataText = Visual::readTextFile(captureMetadata);
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

    std::filesystem::remove_all(root);
    return 0;
}
