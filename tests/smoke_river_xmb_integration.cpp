#include "emulator/RiverXmbIntegration.hpp"

#include <cassert>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

int main() {
    using BMMQ::RiverXmbIntegration;
    using BMMQ::RiverXmbTelemetry;
    const auto generic = BMMQ::makeGameBoyContext("TETRIS");
    assert(generic.presentation == "gameboy");
    assert(generic.artworkKey == "gameboy");
    const auto pokemon = BMMQ::makeGameBoyContext("POKEMON RED");
    assert(pokemon.presentation == "pokemon");
    assert(pokemon.artworkKey == "pokemon-red");
    const auto context = nlohmann::json::parse(RiverXmbIntegration::contextJson(pokemon, "context-set"));
    assert(context["action"] == "context-set");
    assert(context["context"]["presentation"] == "pokemon");
    auto telemetry = BMMQ::simulatedRiverXmbTelemetry(0);
    telemetry.location = "Viridian City";
    telemetry.badges = 2;
    const auto payload = nlohmann::json::parse(RiverXmbIntegration::telemetryJson(telemetry));
    assert(payload["telemetry"]["location"] == "Viridian City");
    assert(payload["telemetry"]["party"][0]["hp"] == 20);
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    setenv("WAYLAND_DISPLAY", "wayland-test", 1);
    const auto path = RiverXmbIntegration::socketPath();
    assert(path.has_value());
    assert(path->find("/tmp/river-xmb-") == 0);
    assert(path->ends_with(".sock"));
    char temporary[] = "/tmp/time-river-test-XXXXXX";
    const auto directory = mkdtemp(temporary);
    assert(directory);
    setenv("XDG_RUNTIME_DIR", directory, 1);
    const auto socketName = *RiverXmbIntegration::socketPath();
    {
        RiverXmbIntegration integration(2);
        integration.telemetryUpdate(BMMQ::simulatedRiverXmbTelemetry(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        for (unsigned i = 1; i <= 100; ++i)
            integration.telemetryUpdate(BMMQ::simulatedRiverXmbTelemetry(i));
        // Let the old unavailable-endpoint send exhaust its retries first.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const int server = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(server >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strcpy(address.sun_path, socketName.c_str());
        assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        assert(listen(server, 8) == 0);
        pollfd ready{server, POLLIN, 0};
        assert(poll(&ready, 1, 2500) == 1);
        const int client = accept(server, nullptr, nullptr);
        assert(client >= 0);
        std::string message;
        char buffer[1024];
        for (;;) {
            pollfd incoming{client, POLLIN, 0};
            assert(poll(&incoming, 1, 1000) == 1);
            const auto size = recv(client, buffer, sizeof(buffer), 0);
            assert(size >= 0);
            if (!size) break;
            message.append(buffer, size);
        }
        close(client);
        const auto received = nlohmann::json::parse(message);
        assert(received["telemetry"]["play_time"] == 100);
        integration.telemetryUpdate(BMMQ::simulatedRiverXmbTelemetry(100));
        assert(poll(&ready, 1, 150) == 0); // Duplicate suppression.
        integration.stop();
        integration.telemetryUpdate(BMMQ::simulatedRiverXmbTelemetry(101));
        assert(poll(&ready, 1, 150) == 0); // No post-stop publication.
        close(server);
    }
    unlink(socketName.c_str());
    std::filesystem::remove(directory);
    {
        RiverXmbIntegration zeroCapacity(0);
        zeroCapacity.telemetryUpdate(telemetry);
        zeroCapacity.stop();
    }
    return 0;
}
