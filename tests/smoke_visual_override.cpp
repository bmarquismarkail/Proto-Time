#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

#include "cores/gameboy/GameBoyMachine.hpp"
#include "cores/gameboy/video/GameBoyVisualDebugAdapter.hpp"
#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VideoService.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

std::unique_ptr<BMMQ::VideoService> makeGameBoyVideoService()
{
    auto videoService = std::make_unique<BMMQ::VideoService>(BMMQ::VideoEngineConfig{
        .frameWidth = 8,
        .frameHeight = 8,
        .queueCapacityFrames = 1,
    });
    videoService->setVisualDebugAdapter(&GB::gameBoyVisualDebugAdapter());
    return videoService;
}

} // namespace

int main()
{
    namespace Visual = BMMQ::Tests::Visual;
    const auto buildFrame = [](BMMQ::VideoService& service,
                               const BMMQ::VideoStateView& state,
                               std::uint64_t generation) {
        return service.engine().buildDebugFrame(Visual::makeSemanticModelFromState(state, 8, 8), generation);
    };

    assert(std::string_view(BMMQ::visualPostEffectKindName(BMMQ::VisualPostEffectKind::Invert)) == "Invert");
    assert(std::string_view(BMMQ::visualPostEffectKindName(BMMQ::VisualPostEffectKind::Grayscale)) == "Grayscale");
    assert(std::string_view(BMMQ::visualPostEffectKindName(BMMQ::VisualPostEffectKind::Multiply)) == "Multiply");
    assert(std::string_view(BMMQ::visualPostEffectKindName(BMMQ::VisualPostEffectKind::AlphaScale)) == "AlphaScale");
    assert(BMMQ::visualPostEffectKindFromString("Invert").value() == BMMQ::VisualPostEffectKind::Invert);
    assert(BMMQ::visualPostEffectKindFromString("invert").value() == BMMQ::VisualPostEffectKind::Invert);
    assert(BMMQ::visualPostEffectKindFromString("Grayscale").value() == BMMQ::VisualPostEffectKind::Grayscale);
    assert(BMMQ::visualPostEffectKindFromString("Multiply").value() == BMMQ::VisualPostEffectKind::Multiply);
    assert(BMMQ::visualPostEffectKindFromString("AlphaScale").value() == BMMQ::VisualPostEffectKind::AlphaScale);
    assert(!BMMQ::visualPostEffectKindFromString("unknown").has_value());

    {
        const BMMQ::VisualAnimationGroup noFrames;
        assert(noFrames.empty());

        BMMQ::VisualAnimationGroup allEmptyFrames;
        allEmptyFrames.frames.emplace_back();
        allEmptyFrames.frames.emplace_back(BMMQ::VisualReplacementImage{
            .width = 2u,
            .height = 0u,
            .argbPixels = {0xFF000000u, 0xFFFFFFFFu, 0xFF000000u, 0xFFFFFFFFu},
        });
        assert(allEmptyFrames.empty());

        BMMQ::VisualAnimationGroup hasContent;
        hasContent.frames.emplace_back(BMMQ::VisualReplacementImage{
            .width = 2u,
            .height = 2u,
            .argbPixels = {0xFF000000u, 0xFFFFFFFFu, 0xFF000000u, 0xFFFFFFFFu},
        });
        assert(!hasContent.empty());
    }

    const auto root = std::filesystem::temp_directory_path() / "bmmq_visual_override_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto state = Visual::makeTileState(0xFFu, 0x00u);
    auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
    assert(resource.has_value());
    assert(resource->descriptor.source.label == "tile_data");
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
    assert(std::holds_alternative<BMMQ::VisualReplacementImage>(resolved->payload));
    const auto& imagePayload = std::get<BMMQ::VisualReplacementImage>(resolved->payload);
    assert(imagePayload.width == 2u);
    assert(imagePayload.height == 2u);
    assert(imagePayload.argbPixels.size() == 4u);
    assert(imagePayload.argbPixels[0] == 0xFFFF0000u);
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

    auto videoService = makeGameBoyVideoService();
    auto originalFrame = buildFrame(*videoService, state, 1u);
    assert(originalFrame.pixels[0] == 0xFF88C070u);

    videoService->setVisualOverrideService(&service);
    std::vector<BMMQ::MachineEventType> visualEvents;
    service.setEventSink([&visualEvents](const BMMQ::MachineEvent& event) {
        assert(event.category == BMMQ::PluginCategory::Video);
        visualEvents.push_back(event.type);
    });
    auto replacedFrame = buildFrame(*videoService, state, 2u);
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

    BMMQ::VisualOverrideService paletteReplaceService;
    const auto paletteReplaceDir = root / "palette-replace-pack";
    Visual::writeTextFile(paletteReplaceDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"palette-replace.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"palette\": [\"0xff101112\", \"0xffdd0000\", \"0xff00dd00\", \"0xff0000dd\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(paletteReplaceService.loadPackManifest(paletteReplaceDir / "pack.json"));
    auto paletteReplaceVideoService = makeGameBoyVideoService();
    paletteReplaceVideoService->setVisualOverrideService(&paletteReplaceService);
    auto paletteReplaceFrame = buildFrame(*paletteReplaceVideoService, state, 11u);
    assert(paletteReplaceFrame.pixels[0] == 0xFFDD0000u);

    BMMQ::VisualOverrideService imagePrecedenceService;
    const auto imagePrecedenceDir = root / "image-precedence-pack";
    Visual::writeBinaryFile(imagePrecedenceDir / "tile.png", Visual::makeSolidPng2x2Rgba(0xFF00FFFFu));
    Visual::writeTextFile(imagePrecedenceDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"image-precedence.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"palette\": [\"0xff101112\", \"0xffdd0000\", \"0xff00dd00\", \"0xff0000dd\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(imagePrecedenceService.loadPackManifest(imagePrecedenceDir / "pack.json"));
    auto imagePrecedenceVideoService = makeGameBoyVideoService();
    imagePrecedenceVideoService->setVisualOverrideService(&imagePrecedenceService);
    auto imagePrecedenceFrame = buildFrame(*imagePrecedenceVideoService, state, 12u);
    assert(imagePrecedenceFrame.pixels[0] == 0xFF00FFFFu);

    BMMQ::VisualOverrideService exactPolicyService;
    const auto exactPolicyDir = root / "exact-policy-pack";
    Visual::writeBinaryFile(exactPolicyDir / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(exactPolicyDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"exact-policy.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"scalePolicy\": \"exact\"\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(exactPolicyService.loadPackManifest(exactPolicyDir / "pack.json"));
    auto exactPolicyVideoService = makeGameBoyVideoService();
    exactPolicyVideoService->setVisualOverrideService(&exactPolicyService);
    auto exactPolicyFrame = buildFrame(*exactPolicyVideoService, state, 6u);
    assert(exactPolicyFrame.pixels[0] == 0xFF88C070u);

    BMMQ::VisualOverrideService cropAnchorService;
    const auto cropAnchorDir = root / "crop-anchor-pack";
    std::vector<uint32_t> cropPixels(16u * 16u, 0xFFFF0000u);
    for (std::size_t y = 8u; y < 16u; ++y) {
        for (std::size_t x = 8u; x < 16u; ++x) {
            cropPixels[y * 16u + x] = 0xFF00FF00u;
        }
    }
    Visual::writeBinaryFile(cropAnchorDir / "tile.png", Visual::makeRgbaPng(16u, 16u, cropPixels));
    Visual::writeTextFile(cropAnchorDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"crop-anchor.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"scalePolicy\": \"crop\",\n"
        "      \"anchor\": \"bottom-right\"\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(cropAnchorService.loadPackManifest(cropAnchorDir / "pack.json"));
    auto cropAnchorVideoService = makeGameBoyVideoService();
    cropAnchorVideoService->setVisualOverrideService(&cropAnchorService);
    auto cropAnchorFrame = buildFrame(*cropAnchorVideoService, state, 7u);
    assert(cropAnchorFrame.pixels[0] == 0xFF00FF00u);

    BMMQ::VisualOverrideService slicingService;
    const auto slicingDir = root / "slicing-pack";
    std::vector<uint32_t> slicingPixels(16u * 16u, 0xFFFF0000u);
    for (std::size_t y = 8u; y < 16u; ++y) {
        for (std::size_t x = 8u; x < 16u; ++x) {
            slicingPixels[y * 16u + x] = 0xFF00FF00u;
        }
    }
    Visual::writeBinaryFile(slicingDir / "tile.png", Visual::makeRgbaPng(16u, 16u, slicingPixels));
    Visual::writeTextFile(slicingDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"slicing.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"slicing\": {\"x\": 8, \"y\": 8, \"width\": 8, \"height\": 8}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(slicingService.loadPackManifest(slicingDir / "pack.json"));
    auto slicingVideoService = makeGameBoyVideoService();
    slicingVideoService->setVisualOverrideService(&slicingService);
    auto slicingFrame = buildFrame(*slicingVideoService, state, 13u);
    assert(slicingFrame.pixels[0] == 0xFF00FF00u);

    BMMQ::VisualOverrideService flipTransformService;
    const auto flipTransformDir = root / "flip-transform-pack";
    Visual::writeBinaryFile(flipTransformDir / "tile.png",
                            Visual::makeRgbaPng(2u, 1u, {0xFFFF0000u, 0xFF00FF00u}));
    Visual::writeTextFile(flipTransformDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"flip-transform.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"transform\": {\"flipX\": true}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(flipTransformService.loadPackManifest(flipTransformDir / "pack.json"));
    auto flipTransformVideoService = makeGameBoyVideoService();
    flipTransformVideoService->setVisualOverrideService(&flipTransformService);
    auto flipTransformFrame = buildFrame(*flipTransformVideoService, state, 14u);
    assert(flipTransformFrame.pixels[0] == 0xFF00FF00u);

    BMMQ::VisualOverrideService rotateTransformService;
    const auto rotateTransformDir = root / "rotate-transform-pack";
    std::vector<uint32_t> rotatePixels{
        0xFFFF0000u, 0xFF00FF00u,
        0xFF0000FFu, 0xFFFFFF00u,
    };
    Visual::writeBinaryFile(rotateTransformDir / "tile.png", Visual::makeRgbaPng(2u, 2u, rotatePixels));
    Visual::writeTextFile(rotateTransformDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"rotate-transform.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"transform\": {\"rotate\": 90}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(rotateTransformService.loadPackManifest(rotateTransformDir / "pack.json"));
    auto rotateTransformVideoService = makeGameBoyVideoService();
    rotateTransformVideoService->setVisualOverrideService(&rotateTransformService);
    auto rotateTransformFrame = buildFrame(*rotateTransformVideoService, state, 15u);
    assert(rotateTransformFrame.pixels[0] == 0xFF0000FFu);

    BMMQ::VisualOverrideService layeredCompositionService;
    const auto layeredCompositionDir = root / "layered-composition-pack";
    Visual::writeBinaryFile(layeredCompositionDir / "base.png", Visual::makeSolidPng2x2Rgba(0xFF0000FFu));
    Visual::writeBinaryFile(layeredCompositionDir / "overlay.png", Visual::makeSolidPng2x2Rgba(0x80FF0000u));
    Visual::writeTextFile(layeredCompositionDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"layered-composition.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"layers\": [\"base.png\", \"overlay.png\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(layeredCompositionService.loadPackManifest(layeredCompositionDir / "pack.json"));
    auto layeredCompositionVideoService = makeGameBoyVideoService();
    layeredCompositionVideoService->setVisualOverrideService(&layeredCompositionService);
    auto layeredCompositionFrame = buildFrame(*layeredCompositionVideoService, state, 16u);
    assert(layeredCompositionFrame.pixels[0] == 0xFF80007Fu);

    BMMQ::VisualOverrideService animationGroupService;
    const auto animationGroupDir = root / "animation-group-pack";
    Visual::writeBinaryFile(animationGroupDir / "frame0.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeBinaryFile(animationGroupDir / "frame1.png", Visual::makeSolidPng2x2Rgba(0xFF00FF00u));
    Visual::writeTextFile(animationGroupDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"animation-group.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"animation\": {\n"
        "        \"frameDuration\": 2,\n"
        "        \"frames\": [\"frame0.png\", \"frame1.png\"]\n"
        "      }\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(animationGroupService.loadPackManifest(animationGroupDir / "pack.json"));
    auto animationGroupVideoService = makeGameBoyVideoService();
    animationGroupVideoService->setVisualOverrideService(&animationGroupService);
    auto animationFrame0 = buildFrame(*animationGroupVideoService, state, 20u);
    auto animationFrame1 = buildFrame(*animationGroupVideoService, state, 21u);
    auto animationFrame2 = buildFrame(*animationGroupVideoService, state, 22u);
    auto animationFrame3 = buildFrame(*animationGroupVideoService, state, 23u);
    assert(animationFrame0.pixels[0] == 0xFFFF0000u);
    assert(animationFrame1.pixels[0] == 0xFFFF0000u);
    assert(animationFrame2.pixels[0] == 0xFF00FF00u);
    assert(animationFrame3.pixels[0] == 0xFF00FF00u);

    BMMQ::VisualOverrideService postEffectsService;
    const auto postEffectsDir = root / "post-effects-pack";
    Visual::writeBinaryFile(postEffectsDir / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFFFFFFu));
    Visual::writeTextFile(postEffectsDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"post-effects.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"effects\": [{\"kind\": \"multiply\", \"argb\": \"0xffff0000\"}]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(postEffectsService.loadPackManifest(postEffectsDir / "pack.json"));
    auto postEffectsVideoService = makeGameBoyVideoService();
    postEffectsVideoService->setVisualOverrideService(&postEffectsService);
    auto postEffectsFrame = buildFrame(*postEffectsVideoService, state, 24u);
    assert(postEffectsFrame.pixels[0] == 0xFFFF0000u);

    BMMQ::VisualOverrideService scriptedEffectsService;
    const auto scriptedEffectsDir = root / "scripted-effects-pack";
    Visual::writeBinaryFile(scriptedEffectsDir / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(scriptedEffectsDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"scripted-effects.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"script\": [\"invert\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(scriptedEffectsService.loadPackManifest(scriptedEffectsDir / "pack.json"));
    auto scriptedEffectsVideoService = makeGameBoyVideoService();
    scriptedEffectsVideoService->setVisualOverrideService(&scriptedEffectsService);
    auto scriptedEffectsFrame = buildFrame(*scriptedEffectsVideoService, state, 25u);
    assert(scriptedEffectsFrame.pixels[0] == 0xFF00FFFFu);

    BMMQ::VisualOverrideService linearPolicyService;
    const auto linearPolicyDir = root / "linear-policy-pack";
    Visual::writeBinaryFile(linearPolicyDir / "tile.png",
                            Visual::makeRgbaPng(2u, 1u, {0xFFFF0000u, 0xFF00FF00u}));
    Visual::writeTextFile(linearPolicyDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"linear-policy.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"tile.png\",\n"
        "      \"scalePolicy\": \"allow-any\",\n"
        "      \"filterPolicy\": \"linear\"\n"
        "    }\n"
        "  }]\n"
        "}\n");
    assert(linearPolicyService.loadPackManifest(linearPolicyDir / "pack.json"));
    auto linearPolicyVideoService = makeGameBoyVideoService();
    linearPolicyVideoService->setVisualOverrideService(&linearPolicyService);
    auto linearPolicyFrame = buildFrame(*linearPolicyVideoService, state, 8u);
    // A 2x1 replacement from red to green is upscaled across the 8x8 tile; pixel 4 lands between the
    // endpoints in the bilinear sample, producing roughly R=0x6D and G=0x92, so the expected ARGB is 0xFF6D9200.
    assert(linearPolicyFrame.pixels[4] == 0xFF6D9200u);

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
    auto signedTileVideoService = makeGameBoyVideoService();
    signedTileVideoService->setVisualOverrideService(&signedTileService);
    auto signedTileFrame = buildFrame(*signedTileVideoService, signedTileState, 3u);
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
    assert(transparentSpriteResource->descriptor.source.label == "sprite_obj");
    BMMQ::VisualOverrideService transparentSpriteService;
    Visual::writeSingleRulePack(root / "transparent-sprite-pack" / "pack.json",
                                "transparent-sprite.gb",
                                BMMQ::VisualResourceKind::Sprite,
                                transparentSpriteResource->descriptor.contentHash,
                                root / "transparent-sprite-pack" / "sprite.png");
    assert(transparentSpriteService.loadPackManifest(root / "transparent-sprite-pack" / "pack.json"));
    auto transparentSpriteVideoService = makeGameBoyVideoService();
    transparentSpriteVideoService->setVisualOverrideService(&transparentSpriteService);
    auto transparentSpriteFrame = buildFrame(*transparentSpriteVideoService, transparentSpriteState, 4u);
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
    auto prioritySpriteVideoService = makeGameBoyVideoService();
    prioritySpriteVideoService->setVisualOverrideService(&prioritySpriteService);
    auto prioritySpriteFrame = buildFrame(*prioritySpriteVideoService, prioritySpriteState, 5u);
    assert(prioritySpriteFrame.pixels[0] == 0xFF88C070u);

    BMMQ::VisualOverrideService backgroundSemanticService;
    const auto backgroundSemanticDir = root / "background-semantic-pack";
    Visual::writeBinaryFile(backgroundSemanticDir / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(backgroundSemanticDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"background-semantic.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"semanticLabel\": \"background_tile_unsigned\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"tile.png\"}\n"
        "  }]\n"
        "}\n");
    assert(backgroundSemanticService.loadPackManifest(backgroundSemanticDir / "pack.json"));
    auto backgroundSemanticVideoService = makeGameBoyVideoService();
    backgroundSemanticVideoService->setVisualOverrideService(&backgroundSemanticService);
    auto backgroundSemanticFrame = buildFrame(*backgroundSemanticVideoService, state, 9u);
    assert(backgroundSemanticFrame.pixels[0] == 0xFFFF0000u);

    auto windowSignedState = Visual::makeTileState(0x00u, 0x00u);
    windowSignedState.lcdc = 0xA1u;
    windowSignedState.wy = 0u;
    windowSignedState.wx = 7u;
    for (std::size_t row = 0; row < 8u; ++row) {
        windowSignedState.vram[0x1000u + row * 2u] = 0xFFu;
        windowSignedState.vram[0x1000u + row * 2u + 1u] = 0x00u;
    }
    windowSignedState.vram[0x1800u] = 0x00u;
    auto windowSignedResource = GB::decodeGameBoyTileResource(windowSignedState, 0x100u, BMMQ::VisualResourceKind::Tile);
    assert(windowSignedResource.has_value());
    BMMQ::VisualOverrideService windowSemanticService;
    const auto windowSemanticDir = root / "window-semantic-pack";
    Visual::writeBinaryFile(windowSemanticDir / "tile.png", Visual::makeSolidPng2x2Rgba(0xFF00FF00u));
    Visual::writeTextFile(windowSemanticDir / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"window-semantic.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"semanticLabel\": \"window_tile_signed\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(windowSignedResource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"tile.png\"}\n"
        "  }]\n"
        "}\n");
    assert(windowSemanticService.loadPackManifest(windowSemanticDir / "pack.json"));
    auto windowSemanticVideoService = makeGameBoyVideoService();
    windowSemanticVideoService->setVisualOverrideService(&windowSemanticService);
    auto windowSemanticFrame = buildFrame(*windowSemanticVideoService, windowSignedState, 10u);
    assert(windowSemanticFrame.pixels[0] == 0xFF00FF00u);

    std::filesystem::remove_all(root);
    return 0;
}
