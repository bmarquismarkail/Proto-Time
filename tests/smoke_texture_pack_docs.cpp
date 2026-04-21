#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "machine/VisualOverrideService.hpp"

namespace {

void advanceWriteTime(const std::filesystem::path& path)
{
    static int offsetSeconds = 1;
    std::error_code ec;
    const auto currentWriteTime = std::filesystem::last_write_time(path, ec);
    assert(!ec);
    std::filesystem::last_write_time(
        path,
        currentWriteTime + std::chrono::seconds(offsetSeconds++),
        ec);
    assert(!ec);
}

void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << contents;
}

} // namespace

int main()
{
    const auto sampleSource = std::filesystem::path(PROTO_TIME_SOURCE_DIR) / "docs" / "texture-pack" / "sample-pack";
    assert(std::filesystem::exists(sampleSource / "pack.json"));
    assert(std::filesystem::exists(sampleSource / "images" / "tile-red.png"));
    assert(std::filesystem::exists(sampleSource / "images" / "tile-green.png"));

    const auto root = std::filesystem::temp_directory_path() / "bmmq_texture_pack_docs_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto sampleRoot = root / "sample-pack";
    std::filesystem::create_directories(sampleRoot / "images");
    std::filesystem::copy_file(sampleSource / "README.md",
                               sampleRoot / "README.md",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(sampleSource / "pack.json",
                               sampleRoot / "pack.json",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(sampleSource / "images" / "tile-red.png",
                               sampleRoot / "images" / "tile-red.png",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(sampleSource / "images" / "tile-green.png",
                               sampleRoot / "images" / "tile-green.png",
                               std::filesystem::copy_options::overwrite_existing);

    const auto manifestPath = sampleRoot / "pack.json";
    const auto redImagePath = sampleRoot / "images" / "tile-red.png";

    BMMQ::VisualOverrideService service;
    assert(service.loadPackManifest(manifestPath));
    assert(service.diagnostics().rulesLoaded == 1u);
    assert(service.diagnostics().missingReplacementImages == 0u);
    assert(service.authorDiagnosticsReport().find("rules loaded: 1") != std::string::npos);

    const auto generationAfterLoad = service.generation();
    assert(!service.reloadChangedPacks());
    assert(service.generation() == generationAfterLoad);
    assert(service.diagnostics().packReloadChecks == 1u);
    assert(service.diagnostics().packReloadsSkipped == 1u);

    writeTextFile(manifestPath,
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"id\": \"docs.sample.gb\",\n"
        "  \"name\": \"Documentation Sample Pack Reloaded\",\n"
        "  \"targets\": [\"gameboy\"],\n"
        "  \"priority\": 101,\n"
        "  \"rules\": [\n"
        "    {\n"
        "      \"match\": {\n"
        "        \"kind\": \"Tile\",\n"
        "        \"decodedHash\": \"0x1111111111111111\",\n"
        "        \"width\": 8,\n"
        "        \"height\": 8\n"
        "      },\n"
        "      \"replace\": {\n"
        "        \"image\": \"images/tile-red.png\"\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n");
    advanceWriteTime(manifestPath);
    assert(service.reloadChangedPacks());
    const auto generationAfterManifestReload = service.generation();
    assert(generationAfterManifestReload == generationAfterLoad + 1u);

    const auto report = service.authorDiagnosticsReport();
    assert(report.find("rules loaded: 1") != std::string::npos);
    assert(report.find("reload checks: 2") != std::string::npos);
    assert(report.find("reloads succeeded: 1") != std::string::npos);

    std::filesystem::remove_all(root);
    return 0;
}
