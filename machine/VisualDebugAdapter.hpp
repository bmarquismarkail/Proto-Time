#ifndef BMMQ_VISUAL_DEBUG_ADAPTER_HPP
#define BMMQ_VISUAL_DEBUG_ADAPTER_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "VideoDebugModel.hpp"
#include "VisualTypes.hpp"

namespace BMMQ {

class Machine;

struct VisualTileSemanticContext {
    std::string_view semanticLabel{};
    bool fromWindow = false;
    std::optional<bool> unsignedTileData;
};

struct VisualTileDecodeRequest {
    std::uint16_t tileIndex = 0;
    std::uint16_t tileAddress = 0;
    std::uint8_t tileX = 0;
    std::uint8_t tileY = 0;
    VisualResourceKind kind = VisualResourceKind::Tile;
    std::uint8_t paletteValue = 0;
    std::string_view paletteRegister{};
    VisualTileSemanticContext semanticContext{};
};

class IVisualDebugAdapter {
public:
    virtual ~IVisualDebugAdapter() = default;
    [[nodiscard]] virtual std::optional<VideoDebugFrameModel> buildFrameModel(
        const Machine& machine,
        const VideoDebugRenderRequest& request) const = 0;
    [[nodiscard]] virtual std::optional<DecodedVisualResource> decodeTile(
        const std::vector<std::uint8_t>& vram,
        std::uint8_t bgp,
        std::uint8_t obp0,
        std::uint8_t obp1,
        const VisualTileDecodeRequest& request) const = 0;
};

} // namespace BMMQ

#endif // BMMQ_VISUAL_DEBUG_ADAPTER_HPP
