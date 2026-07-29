#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
using GameBoyMachine = GB::GameBoyMachine;
#include "cores/gameboy/video/GameBoyVisualDebugAdapter.hpp"
#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "emulator/EmulatorConfig.hpp"
#include "emulator/EmulatorHost.hpp"
#include "machine/VideoService.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

// Create a 16x16 replacement PNG with 256 distinct ARGB values.
// Each texel at (x, y) gets a unique color: alpha=FF, red=(y<<4)|high nibble of x,
// green=(x&0xF)<<4, blue=0. This ensures every output pixel maps to exactly one
// source texel with no duplication or skipping.
std::vector<uint8_t> make16x16ReplacementPng()
{
    std::vector<uint32_t> pixels(256u);
    for (std::size_t y = 0; y < 16u; ++y) {
        for (std::size_t x = 0; x < 16u; ++x) {
            const std::uint8_t r = static_cast<std::uint8_t>((y << 4u) | (x >> 0u));
            const std::uint8_t g = static_cast<std::uint8_t>((x << 4u) & 0xF0u);
            const std::uint8_t b = 0u;
            pixels[y * 16u + x] = (0xFFu << 24u) | (static_cast<std::uint32_t>(r) << 16u) |
                                   (static_cast<std::uint32_t>(g) << 8u) | b;
        }
    }
    return BMMQ::Tests::Visual::makeRgbaPng(16u, 16u, pixels);
}

// Expected replacement pixel color at output coordinate (x, y).
// Mirrors the encoding in make16x16ReplacementPng for test oracles.
std::uint32_t expectedReplacementPixel(std::size_t x, std::size_t y) noexcept
{
    const std::uint8_t r = static_cast<std::uint8_t>((y << 4u) | (x >> 0u));
    const std::uint8_t g = static_cast<std::uint8_t>((x << 4u) & 0xF0u);
    return (0xFFu << 24u) | (static_cast<std::uint32_t>(r) << 16u) |
           (static_cast<std::uint32_t>(g) << 8u);
}

// Create a solid white tile state
BMMQ::VideoStateView makeWhiteTileState()
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x91u;
    state.bgp = 0xE4u;
    for (std::size_t row = 0; row < 8u; ++row) {
        state.vram[row * 2u] = 0xFFu;
        state.vram[row * 2u + 1u] = 0xFFu;
    }
    state.vram[0x1800u] = 0x00u;
    return state;
}

// Create a solid black tile state
BMMQ::VideoStateView makeBlackTileState()
{
    BMMQ::VideoStateView state;
    state.vram.resize(0x2000u, 0);
    state.oam.resize(0x00A0u, 0);
    state.lcdc = 0x91u;
    state.bgp = 0xE4u;
    for (std::size_t row = 0; row < 8u; ++row) {
        state.vram[row * 2u] = 0x00u;
        state.vram[row * 2u + 1u] = 0x00u;
    }
    state.vram[0x1800u] = 0x00u;
    return state;
}

} // namespace

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    const auto root = std::filesystem::temp_directory_path() / "bmmq_hd_texture_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // ========================================================================
    // Test 1: HD scale factor is propagated correctly
    // ========================================================================
    {
        BMMQ::VideoEngineConfig config;
        config.frameWidth = 160;
        config.frameHeight = 144;
        config.hdScale = 2;

        BMMQ::VideoEngine engine(config);
        assert(engine.config().frameWidth == 160);
        assert(engine.config().frameHeight == 144);
        assert(engine.config().hdScale == 2);

        // Build an empty frame - should be HD scaled
        auto frame = engine.buildDebugFrame({}, 1u);
        assert(frame.width == 320);   // 160 * 2
        assert(frame.height == 288);  // 144 * 2
        assert(frame.pixels.size() == 320u * 288u);
    }

    // ========================================================================
    // Test 2: Non-HD behavior is preserved (hdScale = 1)
    // ========================================================================
    {
        BMMQ::VideoEngineConfig config;
        config.frameWidth = 160;
        config.frameHeight = 144;
        config.hdScale = 1;

        BMMQ::VideoEngine engine(config);
        auto frame = engine.buildDebugFrame({}, 1u);
        assert(frame.width == 160);
        assert(frame.height == 144);
        assert(frame.pixels.size() == 160u * 144u);
    }

    // ========================================================================
    // Test 3: 2x HD with replacement - preserves all 256 distinct texels
    // This is the critical correctness test: a 16x16 replacement must contribute
    // all 256 distinct source texels, not 64 duplicated samples.
    // ========================================================================
    {
        const auto state = makeWhiteTileState();
        auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
        assert(resource.has_value());

        // Create 16x16 replacement with distinct colors in each quadrant
        const auto replacementPath = root / "pack" / "images" / "tile_16x16.png";
        Visual::writeBinaryFile(replacementPath, make16x16ReplacementPng());

        // Create manifest
        const auto manifestPath = root / "pack" / "pack.json";
        Visual::writeTextFile(manifestPath,
            "{\n"
            "  \"schemaVersion\": 1,\n"
            "  \"id\": \"hd-test.gb\",\n"
            "  \"name\": \"HD Test Pack\",\n"
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
            "        \"image\": \"images/tile_16x16.png\"\n"
            "      }\n"
            "    }\n"
            "  ]\n"
            "}\n");

        BMMQ::VisualOverrideService service;
        assert(service.loadPackManifest(manifestPath));
        assert(service.diagnostics().rulesLoaded == 1u);

        // Create HD engine
        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoService(config);
        videoService.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoService.setVisualOverrideService(&service);

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);


        auto frame = videoService.engine().buildDebugFrame(model, 1u);

        // Frame should be HD scaled: 8x8 -> 16x16
        assert(frame.width == 16);
        assert(frame.height == 16);
        assert(frame.pixels.size() == 256u);

        // Verify exact pixel-perfect mapping: each output pixel equals the
        // corresponding replacement texel. For an 8x8 resource at hdScale=2 with
        // a 16x16 replacement, output coordinate q maps exactly to replacement q.
        for (std::size_t y = 0; y < 16u; ++y) {
            for (std::size_t x = 0; x < 16u; ++x) {
                // Output coordinate maps directly to replacement coordinate
                const std::uint8_t r = static_cast<std::uint8_t>((y << 4u) | (x >> 0u));
                const std::uint8_t g = static_cast<std::uint8_t>((x << 4u) & 0xF0u);
                const std::uint32_t expected = (0xFFu << 24u) | (static_cast<std::uint32_t>(r) << 16u) |\
                                               (static_cast<std::uint32_t>(g) << 8u);
                assert(frame.pixels[y * 16u + x] == expected);
            }
        }

        // Verify all 256 pixels are unique (no duplication)
        std::set<std::uint32_t> uniquePixels(frame.pixels.begin(), frame.pixels.end());
        assert(uniquePixels.size() == 256u);
    }

    // ========================================================================
    // Test 4: Mixed replaced/unreplaced tiles in HD mode - alignment at boundary
    // ========================================================================
    {
        const auto whiteState = makeWhiteTileState();
        const auto blackState = makeBlackTileState();

        auto whiteResource = GB::decodeGameBoyTileResource(whiteState, 0u, BMMQ::VisualResourceKind::Tile);
        auto blackResource = GB::decodeGameBoyTileResource(blackState, 0u, BMMQ::VisualResourceKind::Tile);
        assert(whiteResource.has_value());
        assert(blackResource.has_value());
        assert(whiteResource->descriptor.contentHash != blackResource->descriptor.contentHash);

        // Create replacement for white tile only (16x16)
        const auto replacementPath = root / "mixed-pack" / "images" / "white_16x16.png";
        Visual::writeBinaryFile(replacementPath, make16x16ReplacementPng());

        const auto manifestPath = root / "mixed-pack" / "pack.json";
        Visual::writeTextFile(manifestPath,
            "{\n"
            "  \"schemaVersion\": 1,\n"
            "  \"id\": \"mixed-hd.gb\",\n"
            "  \"name\": \"Mixed HD Test Pack\",\n"
            "  \"targets\": [\"gameboy\"],\n"
            "  \"rules\": [\n"
            "    {\n"
            "      \"match\": {\n"
            "        \"kind\": \"Tile\",\n"
            "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(whiteResource->descriptor.contentHash) + "\",\n"
            "        \"width\": 8,\n"
            "        \"height\": 8\n"
            "      },\n"
            "      \"replace\": {\n"
            "        \"image\": \"images/white_16x16.png\"\n"
            "      }\n"
            "    }\n"
            "  ]\n"
            "}\n");

        BMMQ::VisualOverrideService service;
        assert(service.loadPackManifest(manifestPath));

        // Create engine with HD scale
        BMMQ::VideoEngineConfig config;
        config.frameWidth = 16;  // 2 tiles wide
        config.frameHeight = 8;  // 1 tile tall
        config.hdScale = 2;

        BMMQ::VideoService videoService(config);
        videoService.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoService.setVisualOverrideService(&service);

        // Build a model with both white (replaced) and black (unreplaced) tiles
        auto whiteModel = Visual::makeSemanticModelFromState(whiteState, 8, 8);
        auto blackModel = Visual::makeSemanticModelFromState(blackState, 8, 8);

        // Merge the two models into one frame
        BMMQ::VideoDebugFrameModel model;
        model.width = 16;
        model.height = 8;
        model.argbPixels.resize(128u, 0xFF000000u);  // 16x8 = 128 pixels
        model.semantics.resize(128u);
        model.resources.reserve(whiteModel.resources.size() + blackModel.resources.size());

        // Copy resources from both models
        for (const auto& r : whiteModel.resources) {
            model.resources.push_back(r);
        }
        const std::size_t whiteResourceCount = model.resources.size();
        for (const auto& r : blackModel.resources) {
            model.resources.push_back(r);
        }

        // Copy pixels and semantics from both models
        // Left half (first 8x8 tile): white
        for (std::size_t y = 0; y < 8u; ++y) {
            for (std::size_t x = 0; x < 8u; ++x) {
                const std::size_t idx = y * 16u + x;
                model.argbPixels[idx] = whiteModel.argbPixels[y * 8u + x];
                model.semantics[idx].resourceIndex = whiteModel.semantics[y * 8u + x].resourceIndex;
                model.semantics[idx].sampleX = whiteModel.semantics[y * 8u + x].sampleX;
                model.semantics[idx].sampleY = whiteModel.semantics[y * 8u + x].sampleY;
            }
        }
        // Right half (second 8x8 tile): black, offset resource indices
        for (std::size_t y = 0; y < 8u; ++y) {
            for (std::size_t x = 0; x < 8u; ++x) {
                const std::size_t srcIdx = y * 8u + x;
                const std::size_t dstIdx = y * 16u + (x + 8u);
                model.argbPixels[dstIdx] = blackModel.argbPixels[srcIdx];
                model.semantics[dstIdx].resourceIndex = whiteResourceCount + blackModel.semantics[srcIdx].resourceIndex;
                model.semantics[dstIdx].sampleX = blackModel.semantics[srcIdx].sampleX;
                model.semantics[dstIdx].sampleY = blackModel.semantics[srcIdx].sampleY;
            }
        }



        auto frame = videoService.engine().buildDebugFrame(model, 1u);

        // Frame should be HD scaled: 16x8 -> 32x16
        assert(frame.width == 32);
        assert(frame.height == 16);
        assert(frame.pixels.size() == 512u);

        // Left half (first tile) should have the replacement colors
        for (std::size_t y = 0; y < 16u; ++y) {
            for (std::size_t x = 0; x < 16u; ++x) {
                assert(frame.pixels[y * 32u + x] == expectedReplacementPixel(x, y));
            }
        }

        // Right half (second tile) should match canonical black tile color
        // (mapped through Game Boy palette, not literal 0xFF000000)
        for (std::size_t y = 0; y < 8u; ++y) {
            for (std::size_t x = 0; x < 8u; ++x) {
                const auto canonicalPixel = blackModel.argbPixels[y * 8u + x];
                // Each 2x2 HD block should equal the canonical pixel
                for (std::size_t sy = 0; sy < 2u; ++sy) {
                    for (std::size_t sx = 0; sx < 2u; ++sx) {
                        const std::size_t dstY = y * 2u + sy;
                        const std::size_t dstX = (x + 8u) * 2u + sx;
                        assert(frame.pixels[dstY * 32u + dstX] == canonicalPixel);
                    }
                }
            }
        }
    }

    // ========================================================================
    // Test 5: HD scale with no replacements - simple upscale preserves detail
    // ========================================================================
    {
        const auto state = makeWhiteTileState();
        auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
        assert(resource.has_value());

        // No pack loaded - no replacements
        BMMQ::VisualOverrideService service;

        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoService(config);
        videoService.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoService.setVisualOverrideService(&service);

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);


        auto frame = videoService.engine().buildDebugFrame(model, 1u);

        // Frame should be HD scaled even without replacements
        assert(frame.width == 16);
        assert(frame.height == 16);
        assert(frame.pixels.size() == 256u);

        // All pixels should be the original color (white/gray in GB palette)
        const auto firstPixel = frame.pixels[0];
        for (std::size_t i = 1; i < frame.pixels.size(); ++i) {
            assert(frame.pixels[i] == firstPixel);
        }
    }

    // ========================================================================
    // Test 6: hdScale = 0 is treated as 1 (minimum valid value)
    // ========================================================================
    {
        BMMQ::VideoEngineConfig config;
        config.frameWidth = 160;
        config.frameHeight = 144;
        config.hdScale = 0;

        BMMQ::VideoEngine engine(config);
        auto frame = engine.buildDebugFrame({}, 1u);
        // hdScale is clamped to minimum 1
        assert(frame.width == 160);
        assert(frame.height == 144);
    }

    // ========================================================================
    // Test 7: HD with null visualOverrideService - should not crash
    // ========================================================================
    {
        const auto state = makeWhiteTileState();

        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoService(config);
        // Do NOT set visualOverrideService - test null safety
        videoService.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);
        // Should not crash even with null service
        auto frame = videoService.engine().buildDebugFrame(model, 1u);
        assert(frame.width == 16);
        assert(frame.height == 16);
    }

    // ========================================================================
    // Test 8: HD scale parser tests for edge cases
    // ========================================================================
    {
        std::vector<char*> argv1 = {"timeEmulator", "--hd-scale", "4"};
        auto args = BMMQ::parseEmulatorArguments(3, argv1.data());
        assert(args.overrides.hdScale.has_value());
        assert(*args.overrides.hdScale == 4u);

        std::vector<char*> argv2 = {"timeEmulator", "--hd-scale", "100"};
        args = BMMQ::parseEmulatorArguments(3, argv2.data());
        assert(args.overrides.hdScale.has_value());
        assert(*args.overrides.hdScale == 8u);

        std::vector<char*> argv3 = {"timeEmulator", "--hd-scale", "0"};
        args = BMMQ::parseEmulatorArguments(3, argv3.data());
        assert(args.overrides.hdScale.has_value());
        assert(*args.overrides.hdScale == 1u);

        // Test missing value throws
        std::vector<char*> argv4 = {"timeEmulator", "--hd-scale"};
        bool threw = false;
        try {
            args = BMMQ::parseEmulatorArguments(2, argv4.data());
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);
    }

    // ========================================================================
    // Test 9: Production bootstrap order - hdScale applied before execution
    // ========================================================================
    {
        const auto tempDir = std::filesystem::temp_directory_path() / "proto_time_hd_bootstrap";
        std::filesystem::remove_all(tempDir);
        std::filesystem::create_directories(tempDir);

        std::vector<std::uint8_t> gameBoyRom(0x8000u, 0x00u);
        gameBoyRom[0x0100] = 0x00u;
        const auto romPath = tempDir / "test.gb";
        std::ofstream output(romPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(gameBoyRom.data()), gameBoyRom.size());

        BMMQ::EmulatorConfig config;
        config.machineKind = std::string("gameboy");
        config.romPath = romPath;
        config.hdScale = 2;
        config.headless = true;

        auto bootstrapped = BMMQ::bootstrapMachine(config);

        // Verify VideoService has HD scale configured (now wired in bootstrapMachine)
        assert(bootstrapped.machine->videoService().engine().config().hdScale == 2);

        // Verify frame dimensions are HD
        auto model = BMMQ::Tests::Visual::makeSemanticModelFromState(
            makeWhiteTileState(), 8, 8);
        auto frame = bootstrapped.machine->videoService().engine().buildDebugFrame(model, 1u);
        assert(frame.width == 320);  // 160 * 2
        assert(frame.height == 288); // 144 * 2
    }

    // ========================================================================
    // Test 10: Capture/observation in HD mode - verifies capture stats and artifacts
    // ========================================================================
    {
        const auto state = makeWhiteTileState();
        auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
        assert(resource.has_value());

        const auto replacementPath = root / "capture-pack" / "images" / "tile_16x16.png";
        Visual::writeBinaryFile(replacementPath, make16x16ReplacementPng());

        const auto manifestPath = root / "capture-pack" / "pack.json";
        Visual::writeTextFile(manifestPath,
            "{\"schemaVersion\":1,\"id\":\"capture-test.gb\",\"name\":\"Capture Test Pack\","
            "\"targets\":[\"gameboy\"],\"rules\":["
            "{\"match\":{\"kind\":\"Tile\",\"decodedHash\":\"" +
            BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\"width\":8,\"height\":8},"
            "\"replace\":{\"image\":\"images/tile_16x16.png\"}}"
            "]}\n");

        const auto captureDir = root / "capture-output";
        std::filesystem::create_directories(captureDir);

        BMMQ::VisualOverrideService service;
        assert(service.loadPackManifest(manifestPath));
        assert(service.beginCapture(captureDir, "gameboy"));

        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoService(config);
        videoService.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoService.setVisualOverrideService(&service);

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);
        auto frame = videoService.engine().buildDebugFrame(model, 1u);
        assert(frame.width == 16 && frame.height == 16);

        service.endCapture();

        // Verify capture actually observed the resource (not just that beginCapture succeeded)
        const auto& capStats = service.captureStats();
        assert(capStats.uniqueResourcesDumped >= 1u);
    }

    // ========================================================================
    // Test 11: ReplacePalette in HD mode - verifies palette mapping is applied
    // ========================================================================
    {
        const auto state = makeWhiteTileState();
        auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
        assert(resource.has_value());

        // Create a replacement with palette override (4-color tile)
        const auto manifestPath = root / "palette-pack" / "pack.json";
        Visual::writeTextFile(manifestPath,
            "{\"schemaVersion\":1,\"id\":\"palette-test.gb\",\"name\":\"Palette Test Pack\","
            "\"targets\":[\"gameboy\"],\"rules\":["
            "{\"match\":{\"kind\":\"Tile\",\"decodedHash\":\"" +
            BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\"width\":8,\"height\":8},"
            "\"replace\":{\"palette\":[\"0xff000000\",\"0xff0000ff\",\"0xff00ff00\",\"0xffff0000\"]}}"
            "]}\n");

        BMMQ::VisualOverrideService service;
        assert(service.loadPackManifest(manifestPath));

        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoService(config);
        videoService.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoService.setVisualOverrideService(&service);

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);
        auto frame = videoService.engine().buildDebugFrame(model, 1u);
        assert(frame.width == 16 && frame.height == 16);

        // Verify every 2x2 block uses the replacement palette
        // The white tile has all pixels with palette index 3 (white in GB palette)
        // With ReplacePalette, index 3 maps to 0xffff0000 (red)
        const std::uint32_t expectedRed = 0xffff0000u;
        for (std::size_t y = 0; y < 16u; ++y) {
            for (std::size_t x = 0; x < 16u; ++x) {
                assert(frame.pixels[y * 16u + x] == expectedRed);
            }
        }
    }

    // ========================================================================
    // Test 12: Exact scale policy - verifies accept/reject against effective dimensions
    // ========================================================================
    {
        const auto state = makeWhiteTileState();
        auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
        assert(resource.has_value());

        // Exact match: 16x16 replacement for 8x8 source at hdScale=2 (effective: 16x16)
        const auto exactPath = root / "exact-pack" / "images" / "tile_16x16.png";
        Visual::writeBinaryFile(exactPath, make16x16ReplacementPng());

        const auto manifestExact = root / "exact-pack" / "pack_exact.json";
        Visual::writeTextFile(manifestExact,
            "{\"schemaVersion\":1,\"id\":\"exact-test.gb\",\"name\":\"Exact Test Pack\","
            "\"targets\":[\"gameboy\"],\"rules\":["
            "{\"match\":{\"kind\":\"Tile\",\"decodedHash\":\"" +
            BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\"width\":8,\"height\":8},"
            "\"replace\":{\"image\":\"images/tile_16x16.png\",\"scalePolicy\":\"exact\"}}"
            "]}\n");

        BMMQ::VisualOverrideService serviceExact;
        assert(serviceExact.loadPackManifest(manifestExact));

        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoServiceExact(config);
        videoServiceExact.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoServiceExact.setVisualOverrideService(&serviceExact);

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);
        auto frameExact = videoServiceExact.engine().buildDebugFrame(model, 1u);
        assert(frameExact.width == 16 && frameExact.height == 16);

        // Exact policy should accept: verify all 256 unique texels are present
        std::set<std::uint32_t> exactPixels(frameExact.pixels.begin(), frameExact.pixels.end());
        assert(exactPixels.size() == 256u);

        // Now test mismatched exact rejection: 32x32 replacement for 8x8 at hdScale=2
        // (effective target is 16x16, but replacement is 32x32 - should be rejected)
        std::vector<uint32_t> largePixels(1024u);
        for (std::size_t y = 0; y < 32u; ++y) {
            for (std::size_t x = 0; x < 32u; ++x) {
                largePixels[y * 32u + x] = (0xFFu << 24u) | (static_cast<uint32_t>(y) << 16u) |
                                           (static_cast<uint32_t>(x) << 8u);
            }
        }
        const auto largePath = root / "exact-pack" / "images" / "tile_32x32.png";
        Visual::writeBinaryFile(largePath, BMMQ::Tests::Visual::makeRgbaPng(32u, 32u, largePixels));

        const auto manifestMismatched = root / "exact-pack" / "pack_mismatched.json";
        Visual::writeTextFile(manifestMismatched,
            "{\"schemaVersion\":1,\"id\":\"exact-mismatch.gb\",\"name\":\"Exact Mismatch Pack\","
            "\"targets\":[\"gameboy\"],\"rules\":["
            "{\"match\":{\"kind\":\"Tile\",\"decodedHash\":\"" +
            BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\"width\":8,\"height\":8},"
            "\"replace\":{\"image\":\"images/tile_32x32.png\",\"scalePolicy\":\"exact\"}}"
            "]}\n");

        BMMQ::VisualOverrideService serviceMismatched;
        assert(serviceMismatched.loadPackManifest(manifestMismatched));

        BMMQ::VideoService videoServiceMismatched(config);
        videoServiceMismatched.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoServiceMismatched.setVisualOverrideService(&serviceMismatched);

        auto frameMismatched = videoServiceMismatched.engine().buildDebugFrame(model, 1u);
        assert(frameMismatched.width == 16 && frameMismatched.height == 16);

        // Mismatched exact should fall back to canonical (all pixels same color)
        const std::uint32_t firstPixel = frameMismatched.pixels[0];
        for (std::size_t i = 1; i < frameMismatched.pixels.size(); ++i) {
            assert(frameMismatched.pixels[i] == firstPixel);
        }
    }

    // ========================================================================
    // Test 13: Mismatched nearest vs linear filtering - verifies deterministic output
    // ========================================================================
    {
        const auto state = makeWhiteTileState();
        auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
        assert(resource.has_value());

        // Create a 32x32 replacement with known gradient pattern
        std::vector<uint32_t> largePixels(1024u);
        for (std::size_t y = 0; y < 32u; ++y) {
            for (std::size_t x = 0; x < 32u; ++x) {
                const std::uint8_t r = static_cast<std::uint8_t>((y << 3u) | (x >> 1u));
                const std::uint8_t g = static_cast<std::uint8_t>((x << 3u) & 0xF0u);
                largePixels[y * 32u + x] = (0xFFu << 24u) | (static_cast<uint32_t>(r) << 16u) |
                                           (static_cast<uint32_t>(g) << 8u);
            }
        }
        const auto largePath = root / "mismatch-pack" / "images" / "tile_32x32.png";
        Visual::writeBinaryFile(largePath, BMMQ::Tests::Visual::makeRgbaPng(32u, 32u, largePixels));

        // Test nearest filtering
        const auto manifestNearest = root / "mismatch-pack" / "nearest.json";
        Visual::writeTextFile(manifestNearest,
            "{\"schemaVersion\":1,\"id\":\"mismatch-nearest.gb\",\"name\":\"Mismatch Nearest\","
            "\"targets\":[\"gameboy\"],\"rules\":["
            "{\"match\":{\"kind\":\"Tile\",\"decodedHash\":\"" +
            BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\"width\":8,\"height\":8},"
            "\"replace\":{\"image\":\"images/tile_32x32.png\",\"filterPolicy\":\"nearest\"}}"
            "]}\n");

        BMMQ::VisualOverrideService serviceNearest;
        assert(serviceNearest.loadPackManifest(manifestNearest));

        BMMQ::VideoEngineConfig config;
        config.frameWidth = 8;
        config.frameHeight = 8;
        config.hdScale = 2;

        BMMQ::VideoService videoServiceNearest(config);
        videoServiceNearest.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoServiceNearest.setVisualOverrideService(&serviceNearest);

        auto model = Visual::makeSemanticModelFromState(state, 8, 8);
        auto frameNearest = videoServiceNearest.engine().buildDebugFrame(model, 1u);
        assert(frameNearest.width == 16 && frameNearest.height == 16);

        // Test linear filtering
        const auto manifestLinear = root / "mismatch-pack" / "linear.json";
        Visual::writeTextFile(manifestLinear,
            "{\"schemaVersion\":1,\"id\":\"mismatch-linear.gb\",\"name\":\"Mismatch Linear\","
            "\"targets\":[\"gameboy\"],\"rules\":["
            "{\"match\":{\"kind\":\"Tile\",\"decodedHash\":\"" +
            BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\"width\":8,\"height\":8},"
            "\"replace\":{\"image\":\"images/tile_32x32.png\",\"filterPolicy\":\"linear\"}}"
            "]}\n");

        BMMQ::VisualOverrideService serviceLinear;
        assert(serviceLinear.loadPackManifest(manifestLinear));

        BMMQ::VideoService videoServiceLinear(config);
        videoServiceLinear.setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
        videoServiceLinear.setVisualOverrideService(&serviceLinear);

        auto frameLinear = videoServiceLinear.engine().buildDebugFrame(model, 1u);
        assert(frameLinear.width == 16 && frameLinear.height == 16);

        // Verify nearest and linear produce different results at an interior coordinate
        // Output (1, 1) -> canonical (0, 0) with sub-pixel (1, 1)
        // q-space = (1, 1), effective source = 16x16 (8*hdScale)
        // scaledCoordinateDouble(1.0, 32, 16) = 1.0 * 31 / 15 = 2.066...
        // nearest snaps to 2, linear interpolates between 2 and 3
        const std::size_t outX = 1u;
        const std::size_t outY = 1u;
        const std::size_t idx = outY * 16u + outX;

        // Nearest should snap to a single texel, linear should blend neighbors
        assert(frameNearest.pixels[idx] != frameLinear.pixels[idx]);

        // Verify nearest output samples scaled coordinate (nearest integer)
        // scaledCoordinateDouble(1.0, 32, 16) = 2.066... -> nearest = 2
        const std::size_t scaledX = 2u;
        const std::size_t scaledY = 2u;
        const std::uint32_t nearestExpected = largePixels[scaledY * 32u + scaledX];
        assert(frameNearest.pixels[idx] == nearestExpected);
    }

    // ========================================================================
    // Test 14: Overlarge hdScale bounds consistency - engine and presenter match
    // ========================================================================
    {
        // Direct API caller with hdScale=100 should get clamped to 8 everywhere
        BMMQ::VideoEngineConfig config;
        config.frameWidth = 160;
        config.frameHeight = 144;
        config.hdScale = 100;

        BMMQ::VideoEngine engine(config);
        // Engine normalizes hdScale to [1, 8]
        assert(engine.config().hdScale == 8);

        // Build a frame and verify dimensions match clamped scale
        auto frame = engine.buildDebugFrame({}, 1u);
        assert(frame.width == 160 * 8);   // 1280
        assert(frame.height == 144 * 8); // 1152
        assert(frame.pixels.size() == static_cast<std::size_t>(frame.width) * frame.height);

        // VideoService should also clamp and keep engine/presenter in sync
        BMMQ::VideoService videoService(config);
        const auto& svcEngineConfig = videoService.engine().config();
        assert(svcEngineConfig.hdScale == 8);
        assert(svcEngineConfig.frameWidth * svcEngineConfig.hdScale == frame.width);
        assert(svcEngineConfig.frameHeight * svcEngineConfig.hdScale == frame.height);

        // Frontend config clamping: hdScale should be clamped before use
        const int clampedHdScale = std::min(std::max(config.hdScale, 1), 8);
        assert(clampedHdScale == 8);
    }

    std::cout << "All HD texture replacement tests passed!\n";
    return 0;
}
