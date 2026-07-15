#include <cassert>
#include <filesystem>
#include <string_view>

#include "machine/plugins/FrontendPluginLoader.hpp"

int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto path = std::filesystem::path(argv[1]);
    BMMQ::FrontendConfig config;
    config.enableVideo = false;
    config.enableAudio = false;
    config.enableInput = false;
    config.autoInitializeBackend = false;

    auto frontend = BMMQ::loadFrontendPlugin(path, config, "bmmq.frontend.glfw");
    assert(frontend->id() == "bmmq.frontend.glfw");
    assert(frontend->displayName() == "GLFW Frontend Plugin");
    assert(frontend->backendName() == "GLFW frontend");
    assert(frontend->config().enableVideo == false);
    assert(frontend->config().enableInput == false);

    const auto expected = BMMQ::defaultFrontendPluginPath(
        std::filesystem::path("/tmp/bin/timeEmulator"),
        BMMQ::kDefaultGlfwFrontendPluginFilename);
    assert(expected == std::filesystem::path("/tmp/bin/libtime-glfw-frontend-plugin.so"));
    assert(BMMQ::normalizeFrontendId("glfw") == "bmmq.frontend.glfw");
    assert(BMMQ::normalizeFrontendId("sdl") == "bmmq.frontend.sdl");
    assert(BMMQ::defaultFrontendPluginFilename("glfw") ==
           BMMQ::kDefaultGlfwFrontendPluginFilename);
    assert(BMMQ::defaultFrontendPluginFilename("bmmq.frontend.sdl") ==
           BMMQ::kDefaultSdlFrontendPluginFilename);
}
