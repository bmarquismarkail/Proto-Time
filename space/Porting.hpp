#pragma once
#include "Project.hpp"
namespace BMMQ::Space {
// Accounting consumes immutable documents. It never owns or resumes a machine.
void validatePorting(const Json &project);
Json mergePorting(const Json &left, const Json &right);
Json checkPorting(const Json &project);
Json queryPorting(const Json &project, const Json &request);
void attachPorting(Json &project, const Json &ledger);
void recordVerification(Json &project, const Json &record);
void verifyPortArtifacts(const Json &project,
                         const std::filesystem::path &source,
                         const std::filesystem::path &target);
std::string portBinding(const Json &ledger);
} // namespace BMMQ::Space
