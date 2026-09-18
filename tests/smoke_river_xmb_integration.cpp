#include "emulator/RiverXmbIntegration.hpp"

#include <cassert>
#include <cstdlib>
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
    RiverXmbTelemetry telemetry;
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
    return 0;
}
