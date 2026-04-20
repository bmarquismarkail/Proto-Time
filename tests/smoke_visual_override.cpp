#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VideoService.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    const auto root = std::filesystem::temp_directory_path() / "bmmq_visual_override_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto state = Visual::makeTileState(0xFFu, 0x00u);
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

    auto differentState = Visual::makeTileState(0x00u, 0xFFu);
    auto different = GB::decodeGameBoyTileResource(differentState, 0u, BMMQ::VisualResourceKind::Tile);
    assert(different.has_value());
    assert(different->descriptor.contentHash != resource->descriptor.contentHash);

    BMMQ::VisualOverrideService service;
    assert(service.enabled());
    const auto imagePath = root / "pack" / "images" / "tile.png";
    Visual::writeBinaryFile(imagePath, Visual::makePng2x2Rgba());
    const auto manifestPath = root / "pack" / "pack.json";
    Visual::writeTextFile(manifestPath,
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

    service.setEnabled(false);
    assert(!service.resolve(resource->descriptor).has_value());
    service.setEnabled(true);

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

    auto signedTileState = Visual::makeTileState(0x00u, 0x00u);
    signedTileState.lcdc = 0x81u;
    for (std::size_t row = 0; row < 8u; ++row) {
        signedTileState.vram[0x1000u + row * 2u] = 0xFFu;
        signedTileState.vram[0x1000u + row * 2u + 1u] = 0x00u;
    }
    signedTileState.vram[0x1800u] = 0x00u;
    auto signedTileResource = GB::decodeGameBoyTileResource(signedTileState, 0x100u, BMMQ::VisualResourceKind::Tile);
    assert(signedTileResource.has_value());
    BMMQ::VisualOverrideService signedTileService;
    Visual::writeSingleRulePack(root / "signed-pack" / "pack.json",
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

    auto transparentSpriteState = Visual::makeTileState(0x00u, 0x00u);
    transparentSpriteState.lcdc = 0x93u;
    transparentSpriteState.oam[0] = 16u;
    transparentSpriteState.oam[1] = 8u;
    transparentSpriteState.oam[2] = 0x00u;
    transparentSpriteState.oam[3] = 0x00u;
    auto transparentSpriteResource =
        GB::decodeGameBoyTileResource(transparentSpriteState, 0u, BMMQ::VisualResourceKind::Sprite);
    assert(transparentSpriteResource.has_value());
    BMMQ::VisualOverrideService transparentSpriteService;
    Visual::writeSingleRulePack(root / "transparent-sprite-pack" / "pack.json",
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

    auto prioritySpriteState = Visual::makeTileState(0xFFu, 0x00u);
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
    Visual::writeSingleRulePack(root / "priority-sprite-pack" / "pack.json",
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
