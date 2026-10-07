#pragma once
#include "Project.hpp"
namespace BMMQ::Space {
// Import text/symbol metadata without executing source or treating labels as proof.
void importSymbols(Json& project,const Json& import,std::span<const uint8_t> rom);
void addPurposeClaim(Json& project,const Json& claim);
void validateMeaning(const Json& project);
Json purposeFindings(const Json& project);
}
