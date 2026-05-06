#include "GameGearMapperFactory.hpp"
#include "GameGearCartridge.hpp"
#include "mappers/Sega3155208Mapper.hpp"
#include "mappers/Sega3155235Mapper.hpp"
#include "mappers/Sega3155365Mapper.hpp"
#include "mappers/CodemastersMapper.hpp"

#include <memory>
#include <algorithm>
#include <climits>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

static std::string toLower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Search for an ASCII substring in the ROM data (case-insensitive).
static bool containsAsciiSubstring(const uint8_t* data, size_t size, const std::string& needleLower) {
    if (data == nullptr || size == 0 || needleLower.empty()) return false;
    const size_t n = needleLower.size();
    if (size < n) return false;
    for (size_t i = 0; i + n <= size; ++i) {
        bool ok = true;
        for (size_t j = 0; j < n; ++j) {
            unsigned char c = data[i + j];
            char lc = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (lc != needleLower[j]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

// External mapping table support.
// The table is a simple TSV with columns: <type>\t<pattern>\t<mapperId>\t[priority]
// Supported types: filename_contains, filename_exact, header_contains
struct MappingEntry {
    enum class Kind { FilenameContains, FilenameExact, HeaderContains } kind;
    std::string patternLower;
    int mapperId = 0;
    int priority = 0;
};

static std::vector<MappingEntry> loadExternalMapperTable() {
    std::vector<MappingEntry> out;

    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / ".internal" / "gamegear_mapper_table.tsv",
        std::filesystem::current_path() / "cores" / "gamegear" / "mapper_table.tsv",
        std::filesystem::path(__FILE__).parent_path() / "mapper_table.tsv",
        std::filesystem::path(__FILE__).parent_path() / "mapper_table.txt",
    };

    for (const auto &p : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) continue;
        std::ifstream ifs(p);
        if (!ifs) continue;
        std::string line;
        while (std::getline(ifs, line)) {
            // trim
            auto l = line;
            while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.erase(l.begin());
            while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
            if (l.empty() || l.front() == '#') continue;
            // split by tabs
            std::vector<std::string> parts;
            size_t pos = 0;
            while (true) {
                size_t tab = l.find('\t', pos);
                if (tab == std::string::npos) { parts.push_back(l.substr(pos)); break; }
                parts.push_back(l.substr(pos, tab - pos));
                pos = tab + 1;
            }
            if (parts.size() < 3) continue;
            const std::string type = parts[0];
            const std::string pattern = parts[1];
            int mid = 0;
            try { mid = std::stoi(parts[2]); } catch (...) { continue; }
            int prio = 0;
            if (parts.size() >= 4) {
                try { prio = std::stoi(parts[3]); } catch (...) { prio = 0; }
            }
            MappingEntry e;
            if (type == "filename_contains") e.kind = MappingEntry::Kind::FilenameContains;
            else if (type == "filename_exact") e.kind = MappingEntry::Kind::FilenameExact;
            else if (type == "header_contains") e.kind = MappingEntry::Kind::HeaderContains;
            else continue;
            e.patternLower = toLower(pattern);
            e.mapperId = mid;
            e.priority = prio;
            out.push_back(std::move(e));
        }
        if (!out.empty()) break; // stop after first table found
    }
    return out;
}

std::unique_ptr<GameGearMapper> createMapperFromRom(const uint8_t* data,
                                                   size_t size,
                                                   const std::optional<std::filesystem::path>& romPath) {
    if (data == nullptr || size == 0u) {
        auto fallback = std::make_unique<GameGearCartridge>();
        (void)fallback->load(nullptr, 0u);
        return fallback;
    }

    // Priority-based heuristics (filename or ROM ASCII content).
    // Each hint group has a priority so that overlapping matches can be
    // ordered deterministically.
    struct Hint { std::vector<std::string> patterns; int mapperId; int priority; };
    const std::vector<Hint> mapperHints = {
        // Higher priority for larger/battery-backed titles
        {{"phantasy", "golden axe", "after burner", "galaxy force", "mercs", "phantasy star", "r-type", "penguin land", "monopoly"}, 5235, 100},
        // 315-5365 hints
        {{"bubble", "bubble bobble", "chase hq", "operation wolf", "wonder boy iii", "wonder boy 3"}, 5365, 90},
        // 315-5208 hints (common Sega mapper)
        {{"alex kidd", "astro", "gangster", "ghostbusters", "the ninja", "world grand prix", "hang-on"}, 5208, 80}
    };

    std::string filenameLower;
    if (romPath.has_value()) {
        filenameLower = toLower(romPath->filename().string());
    }
    // Helper to instantiate mapper by ID
    auto makeMapperById = [](int id) -> std::unique_ptr<GameGearMapper> {
        if (id == 5235) return std::make_unique<Sega3155235Mapper>();
        if (id == 5365) return std::make_unique<Sega3155365Mapper>();
        if (id == 999) return std::make_unique<CodemastersMapper>();
        return std::make_unique<Sega3155208Mapper>();
    };

    // Consult an optional external mapping table first (allows exact filename
    // and header-based overrides). The table format is docs above.
    const auto externalTable = loadExternalMapperTable();
    if (!externalTable.empty()) {
        int bestPriorityExt = INT_MIN;
        int bestMapperIdExt = -1;
        for (const auto &entry : externalTable) {
            bool matched = false;
            switch (entry.kind) {
                case MappingEntry::Kind::FilenameExact:
                    if (!filenameLower.empty() && filenameLower == entry.patternLower) matched = true;
                    break;
                case MappingEntry::Kind::FilenameContains:
                    if (!filenameLower.empty() && filenameLower.find(entry.patternLower) != std::string::npos) matched = true;
                    break;
                case MappingEntry::Kind::HeaderContains: {
                    const size_t headerScan = std::min<size_t>(size, 0x800); // scan small header
                    if (containsAsciiSubstring(data, headerScan, entry.patternLower)) matched = true;
                } break;
            }
            if (matched) {
                if (entry.priority > bestPriorityExt) {
                    bestPriorityExt = entry.priority;
                    bestMapperIdExt = entry.mapperId;
                } else if (bestPriorityExt == INT_MIN) {
                    // if priorities not set (default 0), first match wins
                    bestPriorityExt = entry.priority;
                    bestMapperIdExt = entry.mapperId;
                }
            }
        }
        if (bestMapperIdExt != -1) {
            auto mapper = makeMapperById(bestMapperIdExt);
            if (mapper->load(data, size)) return mapper;
            // fall through to heuristics if mapper fails to load
        }
    }

    // Search filename and content for matches, keeping the highest-priority
    // match if multiple apply.
    int bestPriority = -1;
    int bestMapperId = -1;

    // Filename first
    if (!filenameLower.empty()) {
        for (const auto& hint : mapperHints) {
            for (const auto& pat : hint.patterns) {
                if (filenameLower.find(toLower(pat)) != std::string::npos) {
                    if (hint.priority > bestPriority) {
                        bestPriority = hint.priority;
                        bestMapperId = hint.mapperId;
                    }
                }
            }
        }
    }

    // If none from filename, scan ROM content (prefix) for ASCII title hints
    if (bestPriority < 0) {
        const size_t scanSize = std::min<size_t>(size, 0x20000); // scan up to 128KB
        for (const auto& hint : mapperHints) {
            for (const auto& pat : hint.patterns) {
                const auto needle = toLower(pat);
                if (containsAsciiSubstring(data, scanSize, needle)) {
                    if (hint.priority > bestPriority) {
                        bestPriority = hint.priority;
                        bestMapperId = hint.mapperId;
                    }
                }
            }
        }
    }

    std::unique_ptr<GameGearMapper> mapper;

    if (bestMapperId != -1) {
        mapper = makeMapperById(bestMapperId);
    } else {
        // Fallback to size-based heuristic when no explicit hint found.
        if (size <= 131072u) {
            mapper = makeMapperById(5208);
        } else if (size <= 524288u) {
            mapper = makeMapperById(5235);
        } else {
            mapper = makeMapperById(5365);
        }
    }

    if (!mapper->load(data, size)) {
        auto fallback = std::make_unique<GameGearCartridge>();
        (void)fallback->load(data, size);
        return fallback;
    }
    return mapper;
}
