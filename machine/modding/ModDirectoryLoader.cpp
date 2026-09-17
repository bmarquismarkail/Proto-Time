#include "ModDirectoryLoader.hpp"
#include "PokeredSymbols.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <tuple>

namespace BMMQ::Modding {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maxManifest = 1024 * 1024;
constexpr std::size_t maxData = 64 * 1024 * 1024;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path asset(const std::filesystem::path& root, const std::string& name)
{
    const std::filesystem::path relative(name);
    require(!relative.empty() && !relative.is_absolute(), "asset path must be relative");
    for (const auto& part : relative) require(part != "..", "asset path traversal");
    const auto path = std::filesystem::canonical(root / relative);
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p)
        require(p != path.end() && *p == *r, "asset escapes package directory");
    require(std::filesystem::is_regular_file(path), "asset is not a regular file");
    return path;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path, std::size_t limit)
{
    const auto size = std::filesystem::file_size(path);
    require(size <= limit, "asset exceeds size limit: " + path.string());
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "cannot read asset: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    require(static_cast<std::size_t>(input.gcount()) == size && input.peek() == EOF,
            "asset changed or read failed: " + path.string());
    return bytes;
}

std::string stringField(const Json& value, const char* key)
{
    require(value.contains(key) && value.at(key).is_string(), std::string("missing/string field: ") + key);
    auto text = value.at(key).get<std::string>();
    require(!text.empty() && text.size() <= 4096 && text.find('\0') == std::string::npos,
            std::string("invalid string: ") + key);
    return text;
}

std::uint32_t number(const Json& value, const char* key, std::uint32_t maximum)
{
    require(value.contains(key) && value.at(key).is_number_unsigned(), std::string("unsigned field: ") + key);
    const auto n = value.at(key).get<std::uint64_t>();
    require(n <= maximum, std::string("out of range: ") + key);
    return static_cast<std::uint32_t>(n);
}

void keys(const Json& value, std::initializer_list<std::string_view> allowed)
{
    require(value.is_object(), "expected object");
    for (auto it = value.begin(); it != value.end(); ++it)
        require(std::find(allowed.begin(), allowed.end(), it.key()) != allowed.end(), "unsupported field: " + it.key());
}

std::vector<std::uint8_t> hex(std::string_view text)
{
    require(!text.empty() && text.size() % 2 == 0, "expected nonempty even-length hex");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("invalid hex digit");
    };
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::uint8_t>((digit(text[i]) << 4) | digit(text[i + 1])));
    return result;
}

struct Package { std::filesystem::path root; Json manifest; LoadedMod info; };
} // namespace

ModDirectoryLoadResult loadModDirectories(std::span<const std::filesystem::path> directories,
    std::span<const std::uint8_t> rom, std::string_view machineId)
{
    std::string current;
    try {
        require(directories.size() <= 128, "too many packages");
        require(rom.size() <= maxData, "ROM exceeds size limit");
        // ROM offset translation is explicitly Game Boy-specific for this adapter.
        require(machineId == "gameboy", "unsupported mod ROM adapter");
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned digestSize = 0;
        require(EVP_Digest(rom.data(), rom.size(), digest, &digestSize, EVP_sha256(), nullptr) == 1,
                "SHA-256 failed");
        std::vector<Package> packages;
        std::set<std::string> ids;
        for (const auto& directory : directories) {
            current = directory.string();
            const auto root = std::filesystem::canonical(directory);
            const auto bytes = readFile(asset(root, "manifest.json"), maxManifest);
            // Reject duplicate JSON keys and cap nesting before building the DOM.
            std::vector<std::set<std::string>> objectKeys;
            auto callback = [&objectKeys](int depth, Json::parse_event_t event, Json& value) {
                require(depth < 32, "manifest nesting limit");
                if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
                if (event == Json::parse_event_t::key)
                    require(objectKeys.back().insert(value.get<std::string>()).second, "duplicate JSON key");
                if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
                return true;
            };
            auto manifest = Json::parse(bytes, callback);
            keys(manifest, {"schemaVersion", "id", "version", "priority", "target", "romSha256", "symbols", "regions", "patches", "nativeModule", "trampolines"});
            require(number(manifest, "schemaVersion", 1) == 1, "unsupported schemaVersion");
            require(stringField(manifest, "target") == machineId, "wrong machine target");
            const auto hash = hex(stringField(manifest, "romSha256"));
            require(hash.size() == digestSize && std::equal(hash.begin(), hash.end(), digest), "ROM SHA-256 mismatch");
            LoadedMod info{stringField(manifest, "id"), stringField(manifest, "version"),
                manifest.contains("priority") ? number(manifest, "priority", UINT32_MAX) : 0, {}, {}, {}};
            if (manifest.contains("nativeModule"))
                info.nativeModule = asset(root, stringField(manifest, "nativeModule"));
            if (manifest.contains("trampolines")) {
                require(manifest.at("trampolines").is_array() && manifest.at("trampolines").size() <= 256,
                        "invalid trampolines array");
                for (const auto& t : manifest.at("trampolines")) {
                    keys(t, {"symbol", "hookId"});
                    info.trampolines.push_back({stringField(t, "symbol"), number(t, "hookId", UINT32_MAX)});
                    require(info.trampolines.back().hookId != 0, "trampoline hookId must be nonzero");
                }
            }
            require(info.trampolines.empty() || !info.nativeModule.empty(),
                    "trampolines require nativeModule");
            require(ids.insert(info.id).second, "duplicate mod ID");
            packages.push_back({root, std::move(manifest), std::move(info)});
        }
        std::sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) {
            return std::tie(a.info.priority, a.info.id) < std::tie(b.info.priority, b.info.id);
        });
        PreparedMods prepared;
        prepared.rom.assign(rom.begin(), rom.end());
        std::size_t allocated = 0;
        for (auto& package : packages) {
            current = package.root.string();
            const auto& m = package.manifest;
            if (m.contains("symbols")) {
                const auto path = asset(package.root, stringField(m, "symbols"));
                require(std::filesystem::file_size(path) <= maxManifest * 16, "symbol file too large");
                const auto result = loadPokeredSymbols(path, prepared.host);
                require(result.error.empty() && result.rejected == 0, "invalid or conflicting symbols: " + result.error);
            }
            if (m.contains("regions")) {
                require(m.at("regions").is_array() && m.at("regions").size() <= 1024, "invalid regions array");
                for (const auto& r : m.at("regions")) {
                    keys(r, {"name", "size", "file", "guestBase", "bank"});
                    const auto name = stringField(r, "name");
                    const auto size = number(r, "size", maxData);
                    require(size > 0 && size <= maxData - allocated, "region allocation limit");
                    require(!package.info.regions.contains(name), "duplicate region name");
                    auto data = r.contains("file") ? readFile(asset(package.root, stringField(r, "file")), size) : std::vector<std::uint8_t>{};
                    const auto base = r.contains("guestBase") ? number(r, "guestBase", 65535) : 0;
                    const auto bank = r.contains("bank") ? number(r, "bank", 255) : 0;
                    const auto id = prepared.host.createRegion(package.info.id + ":" + name, size,
                        static_cast<std::uint16_t>(base), static_cast<std::uint8_t>(bank));
                    require(id != 0, "region allocation failed");
                    std::copy(data.begin(), data.end(), prepared.host.region(id).begin());
                    package.info.regions.emplace(name, id);
                    allocated += size;
                }
            }
            if (m.contains("patches")) {
                require(m.at("patches").is_array() && m.at("patches").size() <= 4096, "invalid patches array");
                for (const auto& p : m.at("patches")) {
                    keys(p, {"symbol", "bank", "address", "expected", "replacement"});
                    Symbol target;
                    if (p.contains("symbol")) {
                        require(!p.contains("bank") && !p.contains("address"), "mixed patch target forms");
                        const auto* symbol = prepared.host.resolveSymbol(stringField(p, "symbol"));
                        require(symbol != nullptr, "unknown patch symbol");
                        target = *symbol;
                    } else {
                        target = {static_cast<std::uint8_t>(number(p, "bank", 255)),
                                  static_cast<std::uint16_t>(number(p, "address", 65535))};
                    }
                    const auto expected = hex(stringField(p, "expected"));
                    const auto replacement = hex(stringField(p, "replacement"));
                    require((target.bank == 0 && target.address < 0x4000) ||
                            (target.bank > 0 && target.address >= 0x4000 && target.address < 0x8000), "patch target is not ROM");
                    require(expected.size() <= 0x4000u - (target.address % 0x4000u), "patch crosses ROM bank");
                    const std::size_t offset = std::size_t(target.bank) * 0x4000u + target.address % 0x4000u;
                    require(offset <= rom.size() && expected.size() <= rom.size() - offset, "patch outside ROM");
                    const bool ok = prepared.host.applyPatch(target, expected, replacement,
                        [&](auto, auto, auto bytes) { return std::equal(bytes.begin(), bytes.end(), prepared.rom.begin() + offset); },
                        [&](auto, auto, auto bytes) { std::copy(bytes.begin(), bytes.end(), prepared.rom.begin() + offset); return true; });
                    require(ok, "patch mismatch, size mismatch, or overlap");
                }
            }
            prepared.mods.push_back(std::move(package.info));
        }
        return {std::move(prepared), {}};
    } catch (const std::exception& e) {
        return {std::nullopt, current + ": " + e.what()};
    }
}
} // namespace BMMQ::Modding
