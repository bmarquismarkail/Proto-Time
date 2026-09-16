#include "PokeredSymbols.hpp"

#include <charconv>
#include <fstream>
#include <sstream>

namespace BMMQ::Modding {

SymbolLoadResult loadPokeredSymbols(const std::filesystem::path& path, ModHost& host)
{
    std::ifstream input(path);
    if (!input) return {0u, 0u, "unable to open symbol file"};
    SymbolLoadResult result;
    std::string line;
    while (std::getline(input, line)) {
        line.erase(line.find(';') == std::string::npos ? line.size() : line.find(';'));
        std::istringstream stream(line);
        std::string location;
        std::string name;
        if (!(stream >> location)) continue;
        if (!(stream >> name)) { ++result.rejected; continue; }
        std::string extra;
        if (stream >> extra) { ++result.rejected; continue; }
        const auto colon = location.find(':');
        if (colon == std::string::npos || colon != 2u || location.size() != 7u) {
            ++result.rejected;
            continue;
        }
        unsigned bank = 0u;
        unsigned address = 0u;
        const auto bankText = location.substr(0u, colon);
        const auto addressText = location.substr(colon + 1u);
        const auto bankParse = std::from_chars(bankText.data(), bankText.data() + bankText.size(), bank, 16);
        const auto addressParse = std::from_chars(addressText.data(), addressText.data() + addressText.size(), address, 16);
        if (bankParse.ec != std::errc{} || bankParse.ptr != bankText.data() + bankText.size() ||
            addressParse.ec != std::errc{} || addressParse.ptr != addressText.data() + addressText.size() ||
            bank > 0xFFu || address > 0xFFFFu ||
            !host.addSymbol(name, Symbol{static_cast<std::uint8_t>(bank), static_cast<std::uint16_t>(address)})) {
            ++result.rejected;
            continue;
        }
        ++result.loaded;
    }
    if (input.bad()) result.error = "symbol file read failed";
    return result;
}

} // namespace BMMQ::Modding
