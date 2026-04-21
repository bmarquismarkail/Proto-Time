#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <string>

#include "cores/gameboy/video/GameBoyVisualExtractor.hpp"
#include "machine/VisualOverrideService.hpp"
#include "machine/VisualPackLimits.hpp"
#include "machine/VisualTypes.hpp"
#include "tests/visual_test_helpers.hpp"

namespace {

void advanceWriteTime(const std::filesystem::path& path)
{
    static int offsetSeconds = 1;
    std::error_code ec;
    std::filesystem::last_write_time(
        path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(offsetSeconds++),
        ec);
    assert(!ec);
}

} // namespace

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

    Visual::writeTextFile(root / "schema-missing-pack" / "pack.json",
        "{\n"
        "  \"id\": \"schema-missing.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": []\n"
        "}\n");
    BMMQ::VisualOverrideService missingSchemaService;
    assert(!missingSchemaService.loadPackManifest(root / "schema-missing-pack" / "pack.json"));
    assert(missingSchemaService.lastError().find("unsupported visual pack schemaVersion") != std::string::npos);

    Visual::writeTextFile(root / "schema-unsupported-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 2,\n"
        "  \"id\": \"schema-unsupported.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": []\n"
        "}\n");
    BMMQ::VisualOverrideService unsupportedSchemaService;
    assert(!unsupportedSchemaService.loadPackManifest(root / "schema-unsupported-pack" / "pack.json"));
    assert(unsupportedSchemaService.lastError().find("unsupported visual pack schemaVersion") != std::string::npos);

    const auto multiTargetImagePath = root / "multi-target-pack" / "images" / "tile.png";
    Visual::writeBinaryFile(multiTargetImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "multi-target-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"multi-target.gb\",\n"
        "  \"targets\": [\"nes\", \"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService multiTargetService;
    assert(multiTargetService.loadPackManifest(root / "multi-target-pack" / "pack.json"));
    auto multiTargetResolved = multiTargetService.resolve(resource->descriptor);
    assert(multiTargetResolved.has_value());
    assert(multiTargetResolved->packId == "multi-target.gb");

    const auto sourceHashImagePath = root / "source-hash-pack" / "images" / "tile.png";
    Visual::writeBinaryFile(sourceHashImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "source-hash-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"source-hash.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"sourceHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.sourceHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService sourceHashService;
    assert(sourceHashService.loadPackManifest(root / "source-hash-pack" / "pack.json"));
    auto sourceHashResolved = sourceHashService.resolve(resource->descriptor);
    assert(sourceHashResolved.has_value());
    assert(sourceHashResolved->packId == "source-hash.gb");

    Visual::writeBinaryFile(root / "wrong-source-hash-pack" / "images" / "tile.png", Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "wrong-source-hash-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"wrong-source-hash.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"sourceHash\": \"0xffffffffffffffff\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService wrongSourceHashService;
    assert(wrongSourceHashService.loadPackManifest(root / "wrong-source-hash-pack" / "pack.json"));
    assert(!wrongSourceHashService.resolve(resource->descriptor).has_value());
    assert(wrongSourceHashService.diagnostics().resolveMisses == 1u);

    Visual::writeBinaryFile(root / "metadata-match-pack" / "images" / "tile.png", Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "metadata-match-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"metadata-match.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"sourceHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.sourceHash) + "\",\n"
        "      \"sourceBank\": 0,\n"
        "      \"sourceAddress\": \"0x8000\",\n"
        "      \"tileIndex\": 0,\n"
        "      \"paletteRegister\": \"BGP\",\n"
        "      \"paletteValue\": \"0xe4\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService metadataMatchService;
    assert(metadataMatchService.loadPackManifest(root / "metadata-match-pack" / "pack.json"));
    auto metadataResolved = metadataMatchService.resolve(resource->descriptor);
    assert(metadataResolved.has_value());
    assert(metadataResolved->packId == "metadata-match.gb");
    auto differentTileIndexDescriptor = resource->descriptor;
    differentTileIndexDescriptor.source.index = 1u;
    assert(!metadataMatchService.resolve(differentTileIndexDescriptor).has_value());
    assert(metadataMatchService.diagnostics().resolveMisses == 1u);

    Visual::writeBinaryFile(root / "metadata-miss-pack" / "images" / "tile.png", Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "metadata-miss-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"metadata-miss.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"sourceHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.sourceHash) + "\",\n"
        "      \"sourceAddress\": \"0x8000\",\n"
        "      \"tileIndex\": 1,\n"
        "      \"paletteRegister\": \"BGP\",\n"
        "      \"paletteValue\": \"0xe4\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\"image\": \"images/tile.png\"}\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService metadataMissService;
    assert(metadataMissService.loadPackManifest(root / "metadata-miss-pack" / "pack.json"));
    assert(!metadataMissService.resolve(resource->descriptor).has_value());
    assert(metadataMissService.diagnostics().resolveMisses == 1u);

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

    Visual::writeTextFile(root / "palette-replace-pack" / "pack.json",
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
        "      \"palette\": [\"0xff000000\", \"0xff112233\", \"0xff445566\", \"0xff778899\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService paletteReplaceService;
    assert(paletteReplaceService.loadPackManifest(root / "palette-replace-pack" / "pack.json"));
    const auto paletteReplaceResolved = paletteReplaceService.resolve(resource->descriptor);
    assert(paletteReplaceResolved.has_value());
    assert(paletteReplaceResolved->palette[1] == 0xFF112233u);
    assert(paletteReplaceResolved->image.empty());

    Visual::writeTextFile(root / "palette-replace-invalid-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"palette-replace-invalid.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"palette\": [\"0xff000000\", \"0xff112233\", \"0xff445566\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService paletteReplaceInvalidService;
    assert(paletteReplaceInvalidService.loadPackManifest(root / "palette-replace-invalid-pack" / "pack.json"));
    assert(paletteReplaceInvalidService.diagnostics().invalidRulesSkipped == 1u);
    assert(paletteReplaceInvalidService.diagnostics().rulesLoaded == 0u);

    const auto slicingImagePath = root / "slicing-pack" / "images" / "tile.png";
    Visual::writeBinaryFile(slicingImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "slicing-pack" / "pack.json",
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
        "      \"image\": \"images/tile.png\",\n"
        "      \"slicing\": {\"x\": 1, \"y\": 0, \"width\": 1, \"height\": 2}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService slicingService;
    assert(slicingService.loadPackManifest(root / "slicing-pack" / "pack.json"));
    auto slicingResolved = slicingService.resolve(resource->descriptor);
    assert(slicingResolved.has_value());
    assert(slicingResolved->slice.x == 1u);
    assert(slicingResolved->slice.y == 0u);
    assert(slicingResolved->slice.width == 1u);
    assert(slicingResolved->slice.height == 2u);

    Visual::writeTextFile(root / "slicing-invalid-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"slicing-invalid.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"images/tile.png\",\n"
        "      \"slicing\": {\"x\": 1, \"y\": 0, \"width\": 0, \"height\": 2}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService slicingInvalidService;
    assert(slicingInvalidService.loadPackManifest(root / "slicing-invalid-pack" / "pack.json"));
    assert(slicingInvalidService.diagnostics().invalidRulesSkipped == 1u);
    assert(slicingInvalidService.diagnostics().rulesLoaded == 0u);

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
    const auto missingAssetReport = missingAssetService.authorDiagnosticsReport();
    assert(missingAssetReport.find("missing replacement images: 1") != std::string::npos);
    assert(missingAssetReport.find("replacement load failures: 1") != std::string::npos);

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
    assert(invalidRuleService.authorDiagnosticsReport().find("invalid rules skipped: 1") != std::string::npos);

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

    const auto reloadPackDir = root / "reload-pack";
    const auto reloadManifestPath = reloadPackDir / "pack.json";
    const auto reloadImagePath = reloadPackDir / "tile.png";
    Visual::writeBinaryFile(reloadImagePath, Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(reloadManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"reload-original.gb\",\n"
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
    BMMQ::VisualOverrideService reloadService;
    assert(reloadService.loadPackManifest(reloadManifestPath));
    const auto loadedGeneration = reloadService.generation();
    auto reloadResolved = reloadService.resolve(resource->descriptor);
    assert(reloadResolved.has_value());
    assert(reloadResolved->packId == "reload-original.gb");
    assert(reloadResolved->image.argbPixels[0] == 0xFFFF0000u);
    assert(!reloadService.reloadChangedPacks());
    assert(reloadService.generation() == loadedGeneration);
    assert(reloadService.diagnostics().packReloadChecks == 1u);
    assert(reloadService.diagnostics().packReloadsSkipped == 1u);

    Visual::writeTextFile(reloadManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"reload-updated.gb\",\n"
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
    advanceWriteTime(reloadManifestPath);
    assert(reloadService.reloadChangedPacks());
    assert(reloadService.generation() == loadedGeneration + 1u);
    reloadResolved = reloadService.resolve(resource->descriptor);
    assert(reloadResolved.has_value());
    assert(reloadResolved->packId == "reload-updated.gb");
    assert(reloadService.diagnostics().packReloadsSucceeded == 1u);

    const auto generationBeforeFailedReload = reloadService.generation();
    Visual::writeTextFile(reloadManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"reload-broken.gb\"\n"
        "}\n");
    advanceWriteTime(reloadManifestPath);
    assert(!reloadService.reloadChangedPacks());
    assert(reloadService.generation() == generationBeforeFailedReload);
    assert(reloadService.lastError().find("visual pack reload failed") != std::string::npos);
    reloadResolved = reloadService.resolve(resource->descriptor);
    assert(reloadResolved.has_value());
    assert(reloadResolved->packId == "reload-updated.gb");
    assert(reloadService.diagnostics().packReloadsFailed == 1u);
    assert(reloadService.authorDiagnosticsReport().find("reload failures: 1") != std::string::npos);
    const auto firstReloadWarning = reloadService.takeReloadWarning();
    assert(firstReloadWarning.has_value());
    assert(firstReloadWarning->find("visual pack reload failed") != std::string::npos);
    assert(!reloadService.takeReloadWarning().has_value());

    Visual::writeTextFile(reloadManifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"reload-updated.gb\",\n"
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
    advanceWriteTime(reloadManifestPath);
    assert(reloadService.reloadChangedPacks());
    reloadResolved = reloadService.resolve(resource->descriptor);
    assert(reloadResolved.has_value());
    assert(reloadResolved->image.argbPixels[0] == 0xFFFF0000u);

    const auto generationBeforeAssetReload = reloadService.generation();
    Visual::writeBinaryFile(reloadImagePath, Visual::makeSolidPng2x2Rgba(0xFF00FF00u));
    advanceWriteTime(reloadImagePath);
    assert(reloadService.reloadChangedPacks());
    assert(reloadService.generation() == generationBeforeAssetReload + 1u);
    reloadResolved = reloadService.resolve(resource->descriptor);
    assert(reloadResolved.has_value());
    assert(reloadResolved->packId == "reload-updated.gb");
    assert(reloadResolved->image.argbPixels[0] == 0xFF00FF00u);

    const auto diagnosticPackA = root / "diagnostic-pack-a";
    const auto diagnosticPackB = root / "diagnostic-pack-b";
    Visual::writeBinaryFile(diagnosticPackA / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeBinaryFile(diagnosticPackB / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(diagnosticPackA / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"diagnostic-a.gb\",\n"
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
    Visual::writeTextFile(diagnosticPackB / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"diagnostic-b.gb\",\n"
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
    BMMQ::VisualOverrideService diagnosticReloadService;
    assert(diagnosticReloadService.loadPackManifest(diagnosticPackA / "pack.json"));
    assert(diagnosticReloadService.loadPackManifest(diagnosticPackB / "pack.json"));
    const auto invalidBeforeFailedBatch = diagnosticReloadService.diagnostics().invalidRulesSkipped;
    const auto missingBeforeFailedBatch = diagnosticReloadService.diagnostics().missingReplacementImages;

    Visual::writeTextFile(diagnosticPackA / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"diagnostic-a-reload.gb\",\n"
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
        "    },\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\"image\": \"missing.png\"}\n"
        "    }\n"
        "  ]\n"
        "}\n");
    advanceWriteTime(diagnosticPackA / "pack.json");
    Visual::writeTextFile(diagnosticPackB / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"diagnostic-b-broken.gb\"\n"
        "}\n");
    advanceWriteTime(diagnosticPackB / "pack.json");
    assert(!diagnosticReloadService.reloadChangedPacks());
    assert(diagnosticReloadService.diagnostics().invalidRulesSkipped == invalidBeforeFailedBatch);
    assert(diagnosticReloadService.diagnostics().missingReplacementImages == missingBeforeFailedBatch);

    const auto repeatFailureWarning = diagnosticReloadService.takeReloadWarning();
    assert(repeatFailureWarning.has_value());
    advanceWriteTime(diagnosticPackB / "pack.json");
    assert(!diagnosticReloadService.reloadChangedPacks());
    assert(!diagnosticReloadService.takeReloadWarning().has_value());
    assert(diagnosticReloadService.authorDiagnosticsReport().find("suppressed reload warnings: 1") != std::string::npos);

    std::vector<BMMQ::DecodedVisualResource> cacheResources;
    std::vector<std::filesystem::path> cachePaths;
    const auto cacheRoot = root / "cache-pack";
    for (uint32_t i = 0; i < 5u; ++i) {
        auto cacheState = Visual::makeTileState(static_cast<uint8_t>(0xFFu - i), static_cast<uint8_t>(i));
        auto cacheResource = GB::decodeGameBoyTileResource(cacheState, 0u, BMMQ::VisualResourceKind::Tile);
        assert(cacheResource.has_value());
        cacheResources.push_back(*cacheResource);
        const auto imagePath = cacheRoot / ("tile" + std::to_string(i) + ".png");
        cachePaths.push_back(imagePath);
        std::vector<uint32_t> pixels(2047u * 2047u, 0xFF000000u | (i * 0x00010101u));
        Visual::writeBinaryFile(imagePath, Visual::makeRgbaPng(2047u, 2047u, pixels));
    }

    std::string cacheManifest =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"cache-pack.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [\n";
    for (std::size_t i = 0; i < cacheResources.size(); ++i) {
        cacheManifest +=
            "    {\n"
            "      \"match\": {\n"
            "        \"kind\": \"Tile\",\n"
            "        \"decodedHash\": \"" + BMMQ::toHexVisualHash(cacheResources[i].descriptor.contentHash) + "\",\n"
            "        \"width\": 8,\n"
            "        \"height\": 8\n"
            "      },\n"
            "      \"replace\": {\n"
            "        \"image\": \"" + cachePaths[i].filename().string() + "\"\n"
            "      }\n"
            "    }" + std::string(i + 1u < cacheResources.size() ? "," : "") + "\n";
    }
    cacheManifest += "  ]\n}\n";
    Visual::writeTextFile(cacheRoot / "pack.json", cacheManifest);
    BMMQ::VisualOverrideService cacheService;
    assert(cacheService.loadPackManifest(cacheRoot / "pack.json"));
    for (const auto& cacheResource : cacheResources) {
        auto cacheResolved = cacheService.resolve(cacheResource.descriptor);
        assert(cacheResolved.has_value());
    }
    assert(cacheService.diagnostics().replacementCacheEvictions != 0u);
    assert(cacheService.resolve(cacheResources.front().descriptor).has_value());

    std::filesystem::remove_all(root);
    return 0;
}
