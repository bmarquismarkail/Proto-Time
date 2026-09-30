#pragma once

#include "GameGearVDP.hpp"
#include "machine/Machine.hpp"
#include "machine/VisualDebugAdapter.hpp"

namespace BMMQ {

class GameGearVisualDebugAdapter final : public IVisualDebugAdapter {
public:
    [[nodiscard]] std::optional<VideoDebugFrameModel> buildFrameModel(
        const Machine& machine, const VideoDebugRenderRequest& request) const override {
        const auto state = machine.videoStateSnapshot();
        if (!state.has_value()) return std::nullopt;
        return buildFrameModelFromState(*state, request);
    }

    [[nodiscard]] std::optional<VideoDebugFrameModel> buildFrameModelFromState(
        const VideoStateView& state, const VideoDebugRenderRequest& request) const override {
        if (state.machineId != "gamegear" || state.deviceState.empty()) return std::nullopt;
        GameGearVDP vdp;
        try {
            vdp.importState(state.deviceState);
        } catch (...) {
            return std::nullopt;
        }
        return vdp.buildFrameModel(request);
    }

    [[nodiscard]] std::optional<DecodedVisualResource> decodeTile(
        const std::vector<std::uint8_t>& vram, std::uint8_t,
        std::uint8_t, std::uint8_t,
        const VisualTileDecodeRequest& request) const override {
        constexpr std::size_t kTileBytes = 32u;
        if (request.tileIndex >= 512u || vram.size() != 0x4000u) return std::nullopt;
        DecodedVisualResource resource;
        resource.descriptor.machineId = "gamegear";
        resource.descriptor.kind = request.kind == VisualResourceKind::Unknown
            ? VisualResourceKind::Tile : request.kind;
        resource.descriptor.width = 8u;
        resource.descriptor.height = 8u;
        resource.descriptor.decodedFormat = VisualPixelFormat::Indexed4;
        resource.descriptor.source.index = request.tileIndex;
        resource.descriptor.source.address = request.tileAddress != 0u
            ? request.tileAddress
            : static_cast<std::uint32_t>(request.tileIndex * kTileBytes);
        resource.descriptor.source.paletteValue = request.paletteValue;
        resource.descriptor.source.paletteRegister = std::string(request.paletteRegister);
        resource.descriptor.source.label = std::string(request.semanticContext.semanticLabel);
        resource.pixels.resize(64u);
        for (std::size_t y = 0u; y < 8u; ++y) {
            const auto row = static_cast<std::size_t>(request.tileIndex) * kTileBytes + y * 4u;
            for (std::size_t x = 0u; x < 8u; ++x) {
                const auto bit = static_cast<std::uint8_t>(7u - x);
                const auto color = static_cast<std::uint8_t>(
                    (((vram[row + 3u] >> bit) & 1u) << 3u) |
                    (((vram[row + 2u] >> bit) & 1u) << 2u) |
                    (((vram[row + 1u] >> bit) & 1u) << 1u) |
                    ((vram[row] >> bit) & 1u));
                resource.pixels[y * 8u + x] = color;
            }
        }
        resource.stride = 8u;
        resource.descriptor.sourceHash = hashVisualSourceBytes(resource.pixels);
        resource.descriptor.contentHash = hashDecodedVisualContent(resource);
        return resource;
    }
};

[[nodiscard]] inline const IVisualDebugAdapter& gameGearVisualDebugAdapter() noexcept {
    static const GameGearVisualDebugAdapter adapter;
    return adapter;
}

} // namespace BMMQ
