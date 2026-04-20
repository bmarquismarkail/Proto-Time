#ifndef BMMQ_VISUAL_CAPTURE_WRITER_HPP
#define BMMQ_VISUAL_CAPTURE_WRITER_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "VisualTypes.hpp"

namespace BMMQ {

struct VisualCaptureEntry {
    VisualResourceDescriptor descriptor;
    std::string imagePath;
};

class VisualCaptureWriter {
public:
    [[nodiscard]] static bool writeManifests(const std::filesystem::path& captureDirectory,
                                             std::string_view machineId,
                                             const std::vector<VisualCaptureEntry>& entries,
                                             std::string& error);

    [[nodiscard]] static bool writeDecodedResourcePng(const std::filesystem::path& path,
                                                      const DecodedVisualResource& resource,
                                                      std::string& error);
};

} // namespace BMMQ

#endif // BMMQ_VISUAL_CAPTURE_WRITER_HPP
