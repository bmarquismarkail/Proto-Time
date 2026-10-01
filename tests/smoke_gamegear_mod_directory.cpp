#include "machine/modding/ModDirectoryLoader.hpp"

#include <openssl/evp.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
std::string sha256(const std::vector<std::uint8_t>& bytes)
{
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned size = 0u;
    assert(EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr) == 1);
    std::ostringstream text;
    for (unsigned i = 0u; i < size; ++i) text << std::hex << std::setw(2)
                                                << std::setfill('0') << unsigned(digest[i]);
    return text.str();
}
}

int main()
{
    using namespace BMMQ::Modding;
    auto pattern = (std::filesystem::temp_directory_path() / "time-gg-mod-XXXXXX").string();
    const auto* directory = mkdtemp(pattern.data());
    assert(directory != nullptr);
    const std::filesystem::path root(directory);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};

    std::vector<std::uint8_t> rom(0xC000u, 0xEAu);
    rom[0x4001u] = 0x11u;
    rom[0x8002u] = 0x22u;
    std::ofstream(root / "symbols.sym") << "01:4001 Patched\n02:8002 Target\n";
    std::ofstream(root / "manifest.json")
        << "{\"schemaVersion\":1,\"id\":\"gg-demo\",\"version\":\"1\","
           "\"target\":\"gamegear\",\"romSha256\":\"" << sha256(rom)
        << "\",\"symbols\":\"symbols.sym\",\"patches\":["
           "{\"symbol\":\"Patched\",\"expected\":\"11\",\"replacement\":\"99\"}]}";
    const std::vector<std::filesystem::path> directories{root};
    auto result = loadModDirectories(directories, rom, "gamegear");
    assert(result.prepared && result.error.empty());
    assert(result.prepared->rom[0x4001u] == 0x99u);
    assert(result.prepared->rom[0x8002u] == 0x22u);
    assert(rom[0x4001u] == 0x11u);

    std::ofstream(root / "manifest.json")
        << "{\"schemaVersion\":1,\"id\":\"gg-cross\",\"version\":\"1\","
           "\"target\":\"gamegear\",\"romSha256\":\"" << sha256(rom)
        << "\",\"patches\":[{\"bank\":0,\"address\":16383,"
           "\"expected\":\"eaea\",\"replacement\":\"0000\"}]}";
    assert(!loadModDirectories(directories, rom, "gamegear").prepared);

    std::ofstream(root / "manifest.json")
        << "{\"schemaVersion\":1,\"id\":\"gg-overlap\",\"version\":\"1\","
           "\"target\":\"gamegear\",\"romSha256\":\"" << sha256(rom)
        << "\",\"patches\":[{\"bank\":0,\"address\":0,\"expected\":\"ea\","
           "\"replacement\":\"00\"},{\"bank\":0,\"address\":0,"
           "\"expected\":\"ea\",\"replacement\":\"01\"}]}";
    assert(!loadModDirectories(directories, rom, "gamegear").prepared);
}
