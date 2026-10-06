#pragma once
#include "Project.hpp"
namespace BMMQ::Space {
inline constexpr const char* analyzerVersion="gameboy-hardware-roles-1";
Json hardwareDescriptor(uint64_t location,bool write);
std::string evidenceDigest(const Json&);
Json analyzeProject(Json project,size_t budget=Project::defaultBudget);
void validateAnalysis(const Json&);
Json queryAnalysis(const Json&,const Json& request);
void annotateDocument(Json&,const std::string& instruction,const Json& annotation);
}
