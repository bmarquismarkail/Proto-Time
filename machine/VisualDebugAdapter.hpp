#ifndef BMMQ_VISUAL_DEBUG_ADAPTER_HPP
#define BMMQ_VISUAL_DEBUG_ADAPTER_HPP

#include <cstdint>
#include <optional>
#include <string_view>

#include "VisualTypes.hpp"
#include "plugins/IoPlugin.hpp"

namespace BMMQ {

struct VisualTileSemanticContext {
    std::string_view semanticLabel{};
    bool fromWindow = false;
    std::optional<bool> unsignedTileData;
};

struct VisualTileDecodeRequest {
    uint16_t tileIndex = 0;
    uint16_t tileAddress = 0;
    uint8_t tileX = 0;
    uint8_t tileY = 0;
    VisualResourceKind kind = VisualResourceKind::Tile;
    uint8_t paletteValue = 0;
    std::string_view paletteRegister{};
    VisualTileSemanticContext semanticContext{};
};

class IVisualDebugAdapter {
public:
    virtual ~IVisualDebugAdapter() = default;
    [[nodiscard]] virtual std::optional<DecodedVisualResource> decodeTile(
        const VideoStateView& state,
        const VisualTileDecodeRequest& request) const = 0;
};

} // namespace BMMQ

#endif // BMMQ_VISUAL_DEBUG_ADAPTER_HPP
