#ifndef BMMQ_POKERED_SYMBOLS_HPP
#define BMMQ_POKERED_SYMBOLS_HPP

#include "ModHost.hpp"
#include <filesystem>
#include <string>

namespace BMMQ::Modding {

struct SymbolLoadResult {
    std::size_t loaded = 0;
    std::size_t rejected = 0;
    std::string error;
};

[[nodiscard]] SymbolLoadResult loadPokeredSymbols(const std::filesystem::path& path,
                                                  ModHost& host);

} // namespace BMMQ::Modding

#endif
