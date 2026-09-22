#include "emulator/RiverXmbIntegration.hpp"

#include <cassert>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cstring>
#include <filesystem>
#include <utility>
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
        // Registration waits until River and the matching window are available.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const int server = socket(AF_UNIX, SOCK_STREAM, 0);
        assert(server >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strcpy(address.sun_path, socketName.c_str());
        assert(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        assert(listen(server, 8) == 0);
        auto receiveRequest = [&]() {
            pollfd ready{server, POLLIN, 0};
            assert(poll(&ready, 1, 2500) == 1);
            const int client = accept(server, nullptr, nullptr);
            assert(client >= 0);
            std::string message;
            char byte;
            while (recv(client, &byte, 1, 0) == 1 && byte != '\n') message += byte;
            return std::pair{client, nlohmann::json::parse(message)};
        };
        auto [registrationClient, registration] = receiveRequest();
        assert(registration["action"] == "game-register");
        assert(registration["app_id"] == integration.appId());
        const char* accepted = "{\"ok\":true,\"owner_token\":\"test-token\",\"window_id\":\"test-window\"}\n";
        assert(send(registrationClient, accepted, std::strlen(accepted), MSG_NOSIGNAL) > 0);
        close(registrationClient);
        auto [telemetryClient, received] = receiveRequest();
        assert(received["action"] == "telemetry-update");
        assert(received["owner_token"] == "test-token");
        assert(received["telemetry"]["play_time"] == 100);
        const char* updated = "{\"ok\":true}\n";
        assert(send(telemetryClient, updated, std::strlen(updated), MSG_NOSIGNAL) > 0);
        close(telemetryClient);
        integration.telemetryUpdate(BMMQ::simulatedRiverXmbTelemetry(100));
        pollfd ready{server, POLLIN, 0};
        assert(poll(&ready, 1, 150) == 0); // Duplicate suppression.
        integration.stop();
        auto [clearClient, clear] = receiveRequest();
        assert(clear["action"] == "context-clear");
        assert(clear["owner_token"] == "test-token");
        close(clearClient);
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
