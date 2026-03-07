#ifndef GAMEBOY_PLUGIN_RUNTIME_HPP
#define GAMEBOY_PLUGIN_RUNTIME_HPP

#include "gameboy.hpp"
#include "../../inst_cycle/executor/PluginContract.hpp"

class LR3592_PluginRuntime final : public BMMQ::Plugin::ICpuCoreRuntime {
public:
    const BMMQ::Plugin::PluginMetadata& metadata() const override {
        static const BMMQ::Plugin::PluginMetadata meta{
            sizeof(BMMQ::Plugin::PluginMetadata),
            "bmmq.core.lr3592",
            "LR3592 DMG Core",
            BMMQ::Plugin::PluginKind::CpuCore,
            BMMQ::Plugin::kHostAbiVersion
        };
        return meta;
    }

    BMMQ::Plugin::FetchBlock fetch() override {
        return cpu_.fetch();
    }

    BMMQ::Plugin::ExecutionBlock decode(BMMQ::Plugin::FetchBlock& fb) override {
        return cpu_.decode(fb);
    }

    void execute(const BMMQ::Plugin::ExecutionBlock& block, BMMQ::Plugin::FetchBlock& fb) override {
        cpu_.execute(block, fb);
    }

    const BMMQ::CpuFeedback& getLastFeedback() const override {
        return cpu_.getLastFeedback();
    }

    LR3592_DMG& cpu() { return cpu_; }
    const LR3592_DMG& cpu() const { return cpu_; }

private:
    LR3592_DMG cpu_;
};

#endif // GAMEBOY_PLUGIN_RUNTIME_HPP
