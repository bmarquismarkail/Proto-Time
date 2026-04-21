#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <string>
#include <variant>

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
    assert(std::holds_alternative<BMMQ::VisualReplacementPalette>(paletteReplaceResolved->payload));
    const auto& paletteReplacePayload = std::get<BMMQ::VisualReplacementPalette>(paletteReplaceResolved->payload);
    assert(paletteReplacePayload[1] == 0xFF112233u);

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

    const auto transformImagePath = root / "transform-pack" / "images" / "tile.png";
    Visual::writeBinaryFile(transformImagePath, Visual::makePng2x2Rgba());
    Visual::writeTextFile(root / "transform-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"transform.gb\",\n"
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
        "      \"transform\": {\"flipX\": true, \"flipY\": true, \"rotate\": 270}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService transformService;
    assert(transformService.loadPackManifest(root / "transform-pack" / "pack.json"));
    auto transformResolved = transformService.resolve(resource->descriptor);
    assert(transformResolved.has_value());
    assert(transformResolved->transform.flipX);
    assert(transformResolved->transform.flipY);
    assert(transformResolved->transform.rotateDegrees == 270u);

    Visual::writeTextFile(root / "transform-invalid-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"transform-invalid.gb\",\n"
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
        "      \"transform\": {\"rotate\": 45}\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService transformInvalidService;
    assert(transformInvalidService.loadPackManifest(root / "transform-invalid-pack" / "pack.json"));
    assert(transformInvalidService.diagnostics().invalidRulesSkipped == 1u);
    assert(transformInvalidService.diagnostics().rulesLoaded == 0u);

    const auto layeredBaseImagePath = root / "layered-pack" / "base.png";
    const auto layeredOverlayImagePath = root / "layered-pack" / "overlay.png";
    Visual::writeBinaryFile(layeredBaseImagePath, Visual::makeSolidPng2x2Rgba(0xFF0000FFu));
    Visual::writeBinaryFile(layeredOverlayImagePath, Visual::makeSolidPng2x2Rgba(0x80FF0000u));
    Visual::writeTextFile(root / "layered-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"layered.gb\",\n"
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
    BMMQ::VisualOverrideService layeredService;
    assert(layeredService.loadPackManifest(root / "layered-pack" / "pack.json"));
    auto layeredResolved = layeredService.resolve(resource->descriptor);
    assert(layeredResolved.has_value());
    assert(layeredResolved->mode == BMMQ::VisualOverrideMode::CompositeLayers);
    assert(std::holds_alternative<std::vector<BMMQ::VisualReplacementImage>>(layeredResolved->payload));
    const auto& layeredPayload = std::get<std::vector<BMMQ::VisualReplacementImage>>(layeredResolved->payload);
    assert(layeredPayload.size() == 2u);
    assert(layeredPayload[0].argbPixels[0] == 0xFF0000FFu);
    assert(layeredPayload[1].argbPixels[0] == 0x80FF0000u);

    Visual::writeTextFile(root / "layered-invalid-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"layered-invalid.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"image\": \"base.png\",\n"
        "      \"layers\": [\"overlay.png\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService layeredInvalidService;
    assert(layeredInvalidService.loadPackManifest(root / "layered-invalid-pack" / "pack.json"));
    assert(layeredInvalidService.diagnostics().invalidRulesSkipped == 1u);
    assert(layeredInvalidService.diagnostics().rulesLoaded == 0u);

    Visual::writeBinaryFile(root / "animation-pack" / "frame0.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeBinaryFile(root / "animation-pack" / "frame1.png", Visual::makeSolidPng2x2Rgba(0xFF00FF00u));
    Visual::writeTextFile(root / "animation-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"animation.gb\",\n"
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
        "        \"frameDuration\": 3,\n"
        "        \"frames\": [\"frame0.png\", \"frame1.png\"]\n"
        "      }\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService animationService;
    assert(animationService.loadPackManifest(root / "animation-pack" / "pack.json"));
    auto animationResolved = animationService.resolve(resource->descriptor);
    assert(animationResolved.has_value());
    assert(animationResolved->mode == BMMQ::VisualOverrideMode::AnimationGroup);
    assert(std::holds_alternative<BMMQ::VisualAnimationGroup>(animationResolved->payload));
    const auto& animationPayload = std::get<BMMQ::VisualAnimationGroup>(animationResolved->payload);
    assert(animationPayload.frameDuration == 3u);
    assert(animationPayload.frames.size() == 2u);
    assert(animationPayload.frames[0].argbPixels[0] == 0xFFFF0000u);
    assert(animationPayload.frames[1].argbPixels[0] == 0xFF00FF00u);

    Visual::writeTextFile(root / "animation-invalid-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"animation-invalid.gb\",\n"
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
        "        \"frameDuration\": 0,\n"
        "        \"frames\": [\"frame0.png\", \"frame1.png\"]\n"
        "      }\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService animationInvalidService;
    assert(animationInvalidService.loadPackManifest(root / "animation-invalid-pack" / "pack.json"));
    assert(animationInvalidService.diagnostics().invalidRulesSkipped == 1u);
    assert(animationInvalidService.diagnostics().rulesLoaded == 0u);

    Visual::writeTextFile(root / "animation-too-large-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"animation-too-large.gb\",\n"
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
        "        \"frameDuration\": " +
            std::to_string(BMMQ::VisualPackLimits::kMaxAnimationFrameDuration + 1u) + ",\n"
        "        \"frames\": [\"frame0.png\", \"frame1.png\"]\n"
        "      }\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService animationTooLargeService;
    assert(animationTooLargeService.loadPackManifest(root / "animation-too-large-pack" / "pack.json"));
    assert(animationTooLargeService.diagnostics().invalidRulesSkipped == 1u);
    assert(animationTooLargeService.diagnostics().rulesLoaded == 0u);

    Visual::writeBinaryFile(root / "effects-pack" / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFFFFFFu));
    Visual::writeTextFile(root / "effects-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"effects.gb\",\n"
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
    BMMQ::VisualOverrideService effectsService;
    assert(effectsService.loadPackManifest(root / "effects-pack" / "pack.json"));
    auto effectsResolved = effectsService.resolve(resource->descriptor);
    assert(effectsResolved.has_value());
    assert(effectsResolved->effects.size() == 1u);
    assert(effectsResolved->effects[0].kind == BMMQ::VisualPostEffectKind::Multiply);
    assert(effectsResolved->effects[0].argb == 0xFFFF0000u);

    Visual::writeBinaryFile(root / "script-pack" / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(root / "script-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"script.gb\",\n"
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
        "      \"script\": [\"invert\", \"alpha-scale 0.5\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService scriptService;
    assert(scriptService.loadPackManifest(root / "script-pack" / "pack.json"));
    assert(scriptService.diagnostics().rulesLoaded == 1u);
    auto scriptResolved = scriptService.resolve(resource->descriptor);
    assert(scriptService.diagnostics().resolveMisses == 0u);
    assert(scriptService.diagnostics().replacementLoadFailures == 0u);
    assert(scriptResolved.has_value());
    assert(scriptResolved->effects.size() == 2u);
    assert(scriptResolved->effects[0].kind == BMMQ::VisualPostEffectKind::Invert);
    assert(scriptResolved->effects[1].kind == BMMQ::VisualPostEffectKind::AlphaScale);
    assert(scriptResolved->effects[1].amount == 128u);

    Visual::writeBinaryFile(root / "script-invalid-pack" / "tile.png", Visual::makeSolidPng2x2Rgba(0xFFFF0000u));
    Visual::writeTextFile(root / "script-invalid-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"script-invalid.gb\",\n"
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
        "      \"script\": [\"totally-unknown\"]\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService scriptInvalidService;
    assert(scriptInvalidService.loadPackManifest(root / "script-invalid-pack" / "pack.json"));
    assert(scriptInvalidService.diagnostics().invalidRulesSkipped == 1u);
    assert(scriptInvalidService.diagnostics().rulesLoaded == 0u);

    Visual::writeTextFile(root / "animation-empty-frames-pack" / "pack.json",
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"animation-empty-frames.gb\",\n"
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
        "        \"frameDuration\": 3,\n"
        "        \"frames\": []\n"
        "      }\n"
        "    }\n"
        "  }]\n"
        "}\n");
    BMMQ::VisualOverrideService animationEmptyFramesService;
    assert(animationEmptyFramesService.loadPackManifest(root / "animation-empty-frames-pack" / "pack.json"));
    assert(animationEmptyFramesService.diagnostics().invalidRulesSkipped == 1u);
    assert(animationEmptyFramesService.diagnostics().rulesLoaded == 0u);

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

    constexpr std::size_t oversizedReplacementListCount = 257u;

    std::string tooManyLayers =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"too-many-layers.gb\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"rules\": [{\n"
        "    \"match\": {\n"
        "      \"kind\": \"Tile\",\n"
        "      \"decodedHash\": \"" + BMMQ::toHexVisualHash(resource->descriptor.contentHash) + "\",\n"
        "      \"width\": 8,\n"
        "      \"height\": 8\n"
        "    },\n"
        "    \"replace\": {\n"
        "      \"layers\": [\n";
    for (std::size_t i = 0; i < oversizedReplacementListCount; ++i) {
        tooManyLayers += "        \"missing-layer-" + std::to_string(i) + ".png\"" +
            std::string(i + 1u < oversizedReplacementListCount ? "," : "") + "\n";
    }
    tooManyLayers +=
        "      ]\n"
        "    }\n"
        "  }]\n"
        "}\n";
    Visual::writeTextFile(root / "too-many-layers-pack" / "pack.json", tooManyLayers);
    BMMQ::VisualOverrideService tooManyLayersService;
    assert(tooManyLayersService.loadPackManifest(root / "too-many-layers-pack" / "pack.json"));
    assert(tooManyLayersService.diagnostics().invalidRulesSkipped == 1u);
    assert(tooManyLayersService.diagnostics().missingReplacementImages == 0u);
    assert(tooManyLayersService.diagnostics().rulesLoaded == 0u);

    std::string tooManyAnimationFrames =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"too-many-animation-frames.gb\",\n"
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
        "        \"frameDuration\": 3,\n"
        "        \"frames\": [\n";
    for (std::size_t i = 0; i < oversizedReplacementListCount; ++i) {
        tooManyAnimationFrames += "          \"missing-frame-" + std::to_string(i) + ".png\"" +
            std::string(i + 1u < oversizedReplacementListCount ? "," : "") + "\n";
    }
    tooManyAnimationFrames +=
        "        ]\n"
        "      }\n"
        "    }\n"
        "  }]\n"
        "}\n";
    Visual::writeTextFile(root / "too-many-animation-frames-pack" / "pack.json", tooManyAnimationFrames);
    BMMQ::VisualOverrideService tooManyAnimationFramesService;
    assert(tooManyAnimationFramesService.loadPackManifest(root / "too-many-animation-frames-pack" / "pack.json"));
    assert(tooManyAnimationFramesService.diagnostics().invalidRulesSkipped == 1u);
    assert(tooManyAnimationFramesService.diagnostics().missingReplacementImages == 0u);
    assert(tooManyAnimationFramesService.diagnostics().rulesLoaded == 0u);

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
    assert(std::get<BMMQ::VisualReplacementImage>(reloadResolved->payload).argbPixels[0] == 0xFFFF0000u);
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
    assert(std::get<BMMQ::VisualReplacementImage>(reloadResolved->payload).argbPixels[0] == 0xFFFF0000u);

    const auto generationBeforeAssetReload = reloadService.generation();
    Visual::writeBinaryFile(reloadImagePath, Visual::makeSolidPng2x2Rgba(0xFF00FF00u));
    advanceWriteTime(reloadImagePath);
    assert(reloadService.reloadChangedPacks());
    assert(reloadService.generation() == generationBeforeAssetReload + 1u);
    reloadResolved = reloadService.resolve(resource->descriptor);
    assert(reloadResolved.has_value());
    assert(reloadResolved->packId == "reload-updated.gb");
    assert(std::get<BMMQ::VisualReplacementImage>(reloadResolved->payload).argbPixels[0] == 0xFF00FF00u);

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
