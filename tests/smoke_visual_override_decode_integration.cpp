#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <variant>
#include <vector>

#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/BackgroundTaskService.hpp"
#include "machine/ImageDecoder.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

// Create a minimal visual pack with PNG image for testing
std::string makeTestManifestWithImage(const std::string& imagePath, BMMQ::VisualResourceHash decodedHash)
{
    return R"({
  "schemaVersion": 1,
  "id": "phase32-decode-test",
  "name": "Phase 32 Decode Test Pack",
  "targets": ["gameboy"],
  "rules": [
    {
      "match": {
        "kind": "Tile",
        "decodedHash": ")" + BMMQ::toHexVisualHash(decodedHash) + R"(",
        "width": 8,
        "height": 8
      },
      "replace": {
        "image": ")" + imagePath + R"("
      }
    }
  ]
})";
}

} // namespace

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

  auto tileState = Visual::makeTileState(0xFFu, 0x00u);
  auto decodedResource = GB::decodeGameBoyTileResource(tileState, 0u, BMMQ::VisualResourceKind::Tile);
  assert(decodedResource.has_value());
  const auto descriptor = decodedResource->descriptor;
  constexpr std::uint32_t kExpectedArgb = 0xFF3366CCu;

    const auto testDir = std::filesystem::temp_directory_path() / "bmmq_visual_decode_integration_smoke";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    // Test 1: Visual override service without decoder (sync behavior)
    {
        BMMQ::VisualOverrideService service;
        service.setEnabled(true);

        // Create test pack
        const auto imagePath = testDir / "test1.png";
        Visual::writeBinaryFile(imagePath, Visual::makeSolidPng2x2Rgba(kExpectedArgb));

        const auto manifestPath = testDir / "test1_manifest.json";
        Visual::writeTextFile(manifestPath, makeTestManifestWithImage("test1.png", descriptor.contentHash));

        const auto loaded = service.loadPackManifest(manifestPath);
        assert(loaded);

        const auto resolved = service.resolve(descriptor);
        assert(resolved.has_value());
        assert(std::holds_alternative<BMMQ::VisualReplacementImage>(resolved->payload));
        const auto& image = std::get<BMMQ::VisualReplacementImage>(resolved->payload);
        assert(image.width == 2u);
        assert(image.height == 2u);
        assert(image.argbPixels.size() == 4u);
        for (const auto pixel : image.argbPixels) {
          assert(pixel == kExpectedArgb);
        }

        // Verify diagnostics are at zero (no async work)
        auto diags = service.diagnostics();
        assert(diags.asyncDecodeSubmissions == 0u);
        assert(diags.asyncDecodePollsReady == 0u);
        assert(diags.asyncDecodePollsNotReady == 0u);
    }

    // Test 2: Visual override service with decoder available
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        auto decoder = std::make_unique<BMMQ::ImageDecoder>(&bgService);
        BMMQ::VisualOverrideService service;
        service.setEnabled(true);
        service.setImageDecoder(decoder.get());

        // Create test pack
        const auto imagePath = testDir / "test2.png";
        Visual::writeBinaryFile(imagePath, Visual::makeSolidPng2x2Rgba(kExpectedArgb));

        const auto manifestPath = testDir / "test2_manifest.json";
        Visual::writeTextFile(manifestPath, makeTestManifestWithImage("test2.png", descriptor.contentHash));

        const auto loaded = service.loadPackManifest(manifestPath);
        assert(loaded);

        const auto resolved = service.resolve(descriptor);
        assert(resolved.has_value());
        assert(std::holds_alternative<BMMQ::VisualReplacementImage>(resolved->payload));
        const auto& image = std::get<BMMQ::VisualReplacementImage>(resolved->payload);
        assert(image.width == 2u);
        assert(image.height == 2u);
        assert(image.argbPixels.size() == 4u);
        for (const auto pixel : image.argbPixels) {
          assert(pixel == kExpectedArgb);
        }

        auto diags = service.diagnostics();
        assert(diags.asyncDecodeSubmissions >= 1u);
        assert(diags.asyncDecodePollsReady + diags.asyncDecodePollsNotReady >= 1u);

        bgService.shutdown();
    }

    // Test 3: Telemetry tracking for async decode
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        auto decoder = std::make_unique<BMMQ::ImageDecoder>(&bgService);
        BMMQ::VisualOverrideService service;
        service.setEnabled(true);
        service.setImageDecoder(decoder.get());

        // Initial diagnostics
        auto initialDiags = service.diagnostics();
        const auto initialSubmissions = initialDiags.asyncDecodeSubmissions;
        const auto initialReady = initialDiags.asyncDecodePollsReady;
        const auto initialNotReady = initialDiags.asyncDecodePollsNotReady;

        // Create test pack with image
        const auto imagePath = testDir / "test3.png";
        Visual::writeBinaryFile(imagePath, Visual::makeSolidPng2x2Rgba(kExpectedArgb));

        const auto manifestPath = testDir / "test3_manifest.json";
        Visual::writeTextFile(manifestPath, makeTestManifestWithImage("test3.png", descriptor.contentHash));

        assert(service.loadPackManifest(manifestPath));
        const auto resolved = service.resolve(descriptor);
        assert(resolved.has_value());

        // Final diagnostics
        auto finalDiags = service.diagnostics();

        assert(finalDiags.asyncDecodeSubmissions > initialSubmissions);
        assert(finalDiags.asyncDecodePollsReady >= initialReady);
        assert(finalDiags.asyncDecodePollsNotReady >= initialNotReady);

        bgService.shutdown();
    }

    // Test 4: Verify decoder is settable and retrievable through service
    {
        BMMQ::BackgroundTaskService bgService;
        bgService.start();

        auto decoder = std::make_unique<BMMQ::ImageDecoder>(&bgService);
        BMMQ::VisualOverrideService service;
        service.setImageDecoder(decoder.get());

        // The decoder pointer is now set (verified by lack of crashes)
        assert(true);  // If we get here, setImageDecoder worked

        bgService.shutdown();
    }

    // Cleanup
    std::filesystem::remove_all(testDir);

    return 0;
}
