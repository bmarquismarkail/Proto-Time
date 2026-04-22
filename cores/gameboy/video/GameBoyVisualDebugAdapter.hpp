#ifndef GB_GAMEBOY_VISUAL_DEBUG_ADAPTER_HPP
#define GB_GAMEBOY_VISUAL_DEBUG_ADAPTER_HPP

#include "../../../machine/VisualDebugAdapter.hpp"
#include "GameBoyVisualExtractor.hpp"
#include <optional>
#include <cstddef>

namespace GB {

class GameBoyVisualDebugAdapter final : public BMMQ::IVisualDebugAdapter {
public:
    [[nodiscard]] std::optional<BMMQ::DecodedVisualResource> decodeTile(
        const BMMQ::VideoStateView& state,
        const BMMQ::VisualTileDecodeRequest& request) const override
    {
        if (request.tileAddress < 0x8000u) {
            return std::nullopt;
        }

        GameBoyVisualSemanticContext semanticContext{
            .semanticLabel = request.semanticContext.semanticLabel,
            .fromWindow = request.semanticContext.fromWindow,
            .hasTileDataMode = request.semanticContext.unsignedTileData.has_value(),
            .unsignedTileData = request.semanticContext.unsignedTileData.value_or(true),
        };

        return decodeGameBoyTileResourceAtVramOffset(state,
                                                     static_cast<std::size_t>(request.tileAddress - 0x8000u),
                                                     request.tileIndex,
                                                     request.kind,
                                                     request.paletteValue,
                                                     request.paletteRegister,
                                                     semanticContext);
    }
};

[[nodiscard]] inline const BMMQ::IVisualDebugAdapter& gameBoyVisualDebugAdapter()
{
    static const GameBoyVisualDebugAdapter adapter;
    return adapter;
}

} // namespace GB

#endif // GB_GAMEBOY_VISUAL_DEBUG_ADAPTER_HPP
