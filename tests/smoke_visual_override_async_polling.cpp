#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <thread>

#include "machine/VisualOverrideService.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

// Helper to create a simple valid visual pack manifest
std::string makeTestManifest()
{
    return R"({
  "schemaVersion": 1,
  "id": "test-pack",
  "name": "Test Pack",
  "targets": ["gameboy"],
  "rules": [
    {
      "match": {
        "kind": "Tile",
        "decodedHash": "0000000000000000",
        "width": 8,
        "height": 8
      },
      "replace": {
        "palette": ["0xff000000", "0xffffffff", "0xff888888", "0xffcccccc"]
      }
    }
  ]
})";
}

} // namespace

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    // Test 1: watchedReloadPaths returns empty for no packs
    {
        BMMQ::VisualOverrideService service;
        const auto watched = service.watchedReloadPaths();
        assert(watched.empty());
    }

    // Test 2: watchedReloadPaths returns loaded pack manifests
    {
        const auto root = std::filesystem::temp_directory_path() / "bmmq_visual_async_polling_smoke";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);

        // Create manifest
        const auto manifestPath = root / "pack.json";
        Visual::writeTextFile(manifestPath, makeTestManifest());

        BMMQ::VisualOverrideService service;
        service.setEnabled(true);

        // Load the pack
        const auto loaded = service.loadPackManifest(manifestPath);
        assert(loaded);

        // Get watched paths - should include the manifest
        const auto watched = service.watchedReloadPaths();
        assert(!watched.empty());

        // Verify at least one watched path matches our manifest
        bool foundManifest = false;
        for (const auto& watchedPath : watched) {
            if (watchedPath == manifestPath) {
                foundManifest = true;
                break;
            }
        }
        assert(foundManifest);

        // Cleanup
        std::filesystem::remove_all(root);
    }

    // Test 3: Telemetry recording methods work
    {
        BMMQ::VisualOverrideService service;
        auto diags = service.diagnostics();
        const auto initial_submissions = diags.asyncProbeSubmissions;
        const auto initial_changes = diags.asyncProbeChangesDetected;
        const auto initial_applies = diags.asyncProbeReloadApplies;

        // Record telemetry
        service.recordAsyncProbeSubmission();
        service.recordAsyncProbeChangeDetected();
        service.recordAsyncProbeReloadApplied();

        diags = service.diagnostics();
        assert(diags.asyncProbeSubmissions == initial_submissions + 1);
        assert(diags.asyncProbeChangesDetected == initial_changes + 1);
        assert(diags.asyncProbeReloadApplies == initial_applies + 1);
    }

    // Test 4: Telemetry increments multiple times
    {
        BMMQ::VisualOverrideService service;
        for (int i = 0; i < 5; ++i) {
            service.recordAsyncProbeSubmission();
            service.recordAsyncProbeChangeDetected();
            service.recordAsyncProbeReloadApplied();
        }
        auto diags = service.diagnostics();
        assert(diags.asyncProbeSubmissions == 5u);
        assert(diags.asyncProbeChangesDetected == 5u);
        assert(diags.asyncProbeReloadApplies == 5u);
    }

    return 0;
}
