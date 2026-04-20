#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <string>

#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualPackLimits.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

int main()
{
    namespace Visual = BMMQ::Tests::Visual;

    const auto root = std::filesystem::temp_directory_path() / "bmmq_visual_manifest_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    auto state = Visual::makeTileState(0xFFu, 0x00u);
    auto resource = GB::decodeGameBoyTileResource(state, 0u, BMMQ::VisualResourceKind::Tile);
    assert(resource.has_value());

    auto differentPaletteState = state;
    differentPaletteState.bgp = 0x1Bu;
    auto differentPalette = GB::decodeGameBoyTileResource(differentPaletteState, 0u, BMMQ::VisualResourceKind::Tile);
    assert(differentPalette.has_value());

    const auto semanticImagePath = root / "semantic-pack" / "images" / "tile.png";
    Visual::writeBinaryFile(semanticImagePath, Visual::makePng2x2Rgba());
    const auto semanticManifestPath = root / "semantic-pack" / "pack.json";
    Visual::writeTextFile(semanticManifestPath,
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

    Visual::writeBinaryFile(root / "priority-low-pack" / "tile.png", Visual::makePng2x2Rgba());
    Visual::writeBinaryFile(root / "priority-high-pack" / "tile.png", Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "priority-low-pack" / "pack.json",
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
    Visual::writeTextFile(root / "priority-high-pack" / "pack.json",
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
    Visual::writeBinaryFile(paletteImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "palette-pack" / "pack.json",
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
        "      \"replace\": {\"image\": \"images/tile.png\"}\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService paletteService;
    assert(paletteService.loadPackManifest(root / "palette-pack" / "pack.json"));
    assert(paletteService.resolve(resource->descriptor).has_value());
    assert(!paletteService.resolve(differentPalette->descriptor).has_value());
    assert(paletteService.diagnostics().resolveMisses == 1u);

    const auto paletteHashImagePath = root / "palette-hash-pack" / "images" / "tile.png";
    Visual::writeBinaryFile(paletteHashImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "palette-hash-pack" / "pack.json",
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
        "      \"replace\": {\"image\": \"images/tile.png\"}\n"
        "    }\n"
        "  ]\n"
        "}\n");
    BMMQ::VisualOverrideService paletteHashService;
    assert(paletteHashService.loadPackManifest(root / "palette-hash-pack" / "pack.json"));
    assert(paletteHashService.resolve(resource->descriptor).has_value());
    assert(!paletteHashService.resolve(differentPalette->descriptor).has_value());
    assert(paletteHashService.diagnostics().resolveMisses == 1u);

    Visual::writeTextFile(root / "pack" / "bad.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"bad.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/missing.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService missingAssetService;
    assert(missingAssetService.loadPackManifest(root / "pack" / "bad.json"));
    assert(!missingAssetService.resolve(resource->descriptor).has_value());
    assert(missingAssetService.diagnostics().missingReplacementImages == 1u);

    Visual::writeTextFile(root / "pack" / "invalid-rules.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"invalid-rules.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService invalidRuleService;
    assert(invalidRuleService.loadPackManifest(root / "pack" / "invalid-rules.json"));
    assert(invalidRuleService.diagnostics().invalidRulesSkipped == 1u);
    assert(invalidRuleService.diagnostics().rulesLoaded == 0u);

    const auto unicodeImagePath = root / "unicode-pack" / "tile.png";
    Visual::writeBinaryFile(unicodeImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "unicode-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"unicode.\\u0041.\\uD83D\\uDE00\",\n"
        "  \"name\": \"Unicode Pack\",\n"
        "  \"targets\": [\"gameboy\"],\n"
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
    BMMQ::VisualOverrideService unicodePackService;
    assert(unicodePackService.loadPackManifest(root / "unicode-pack" / "pack.json"));
    auto unicodeResolved = unicodePackService.resolve(resource->descriptor);
    assert(unicodeResolved.has_value());
    assert(unicodeResolved->packId == std::string("unicode.A.") + "\xF0\x9F\x98\x80");

    Visual::writeTextFile(root / "pack" / "garbage-hash.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"garbage-hash.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "garbage\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService garbageHashService;
    assert(garbageHashService.loadPackManifest(root / "pack" / "garbage-hash.json"));
    assert(garbageHashService.diagnostics().invalidRulesSkipped == 1u);
    assert(garbageHashService.diagnostics().rulesLoaded == 0u);

    Visual::writeTextFile(root / "oversized-pack" / "pack.json",
                          std::string(BMMQ::VisualPackLimits::kMaxManifestBytes + 1u, ' '));
    BMMQ::VisualOverrideService oversizedManifestService;
    assert(!oversizedManifestService.loadPackManifest(root / "oversized-pack" / "pack.json"));
    assert(oversizedManifestService.lastError().find("visual pack manifest too large") != std::string::npos);

    std::string tooManyRules =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"too-many-rules.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n";
    for (std::size_t i = 0; i < BMMQ::VisualPackLimits::kMaxManifestRules + 1u; ++i) {
        tooManyRules +=
            "    {\n"
            "      \"match\": {\n"
            "        \"kind\": \"Tile\",\n"
            "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
            "        \"width\": 8,\n"
            "        \"height\": 8\n"
            "      },\n"
            "      \"replace\": {\"image\": \"images/tile.png\"}\n"
            "    }" +
            std::string(i + 1u < BMMQ::VisualPackLimits::kMaxManifestRules + 1u ? "," : "") + "\n";
    }
    tooManyRules += "  ]\n}\n";
    Visual::writeTextFile(root / "too-many-rules" / "pack.json", tooManyRules);
    BMMQ::VisualOverrideService tooManyRulesService;
    assert(!tooManyRulesService.loadPackManifest(root / "too-many-rules" / "pack.json"));
    assert(tooManyRulesService.lastError().find("too many visual pack rules") != std::string::npos);

    const auto oversizedImagePath = root / "oversized-image-pack" / "large.png";
    Visual::writeBinaryFile(oversizedImagePath,
                            Visual::makeHeaderOnlyPng(BMMQ::VisualPackLimits::kMaxReplacementImageDimension + 1u, 1u));
    Visual::writeTextFile(root / "oversized-image-pack" / "pack.json",
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
    Visual::writeBinaryFile(oversizedInflateImagePath, Visual::makeHeaderOnlyPng(2048u, 2048u));
    Visual::writeTextFile(root / "oversized-inflate-pack" / "pack.json",
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

    std::filesystem::remove_all(root);
    return 0;
}
