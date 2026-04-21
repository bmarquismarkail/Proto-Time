#ifndef BMMQ_HEADLESS_FRAME_DUMPER_HPP
#define BMMQ_HEADLESS_FRAME_DUMPER_HPP

#include <new>
#include <string_view>
#include <vector>

#include "../VideoPlugin.hpp"

namespace BMMQ {

class HeadlessFrameDumper final : public IVideoPresenterPlugin,
                                  public IVideoCapturePlugin {
public:
    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "headless";
    }

    [[nodiscard]] VideoPluginCapabilities capabilities() const noexcept override
    {
        return {
            .realtimeSafe = false,
            .frameSizePreserving = true,
            .snapshotAware = true,
            .deterministic = true,
            .headlessSafe = true,
        };
    }

    bool open(const VideoPresenterConfig&) override
    {
        ready_ = true;
        return true;
    }

    void close() noexcept override
    {
        ready_ = false;
    }

    [[nodiscard]] bool ready() const noexcept override
    {
        return ready_;
    }

    bool present(const VideoFramePacket& frame) noexcept override
    {
        try {
            frames_.push_back(frame);
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        }
    }

    bool capture(const VideoFramePacket& frame) noexcept override
    {
        try {
            frames_.push_back(frame);
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        }
    }

    [[nodiscard]] std::string_view lastError() const noexcept override
    {
        return {};
    }

    [[nodiscard]] const std::vector<VideoFramePacket>& presentedFrames() const noexcept
    {
        return frames_;
    }

private:
    bool ready_ = false;
    std::vector<VideoFramePacket> frames_{};
};

} // namespace BMMQ

#endif // BMMQ_HEADLESS_FRAME_DUMPER_HPP
