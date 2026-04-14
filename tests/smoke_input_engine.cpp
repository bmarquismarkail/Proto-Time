#include <cassert>

#include "machine/plugins/input/InputEngine.hpp"

int main()
{
    BMMQ::InputEngine engine({
        .stagingCapacity = 2,
    });

    assert(engine.stageDigitalSnapshot(0x01u, 1u));
    assert(engine.stageDigitalSnapshot(0x03u, 1u));
    assert(engine.commitDigitalSnapshot());
    assert(engine.committedDigitalMask().has_value());
    assert(*engine.committedDigitalMask() == 0x03u);
    assert(engine.stats().lastCommittedGeneration == 1u);

    assert(engine.stageDigitalSnapshot(0x04u, 1u));
    engine.advanceGeneration(2u);
    assert(!engine.commitDigitalSnapshot());
    assert(engine.stats().staleGenerationDropCount == 1u);

    assert(engine.stageDigitalSnapshot(0x01u, 2u));
    assert(engine.stageDigitalSnapshot(0x02u, 2u));
    assert(engine.stageDigitalSnapshot(0x08u, 2u));
    assert(engine.stats().eventOverflowCount == 1u);
    assert(engine.stats().digitalOverflowCount == 1u);
    assert(engine.commitDigitalSnapshot());
    assert(engine.committedDigitalMask().has_value());
    assert((*engine.committedDigitalMask() == 0x08u)
           && "input engine did not coalesce overflow to the latest complete digital snapshot");

    BMMQ::InputAnalogState analog{};
    analog.channels[0] = 123;
    analog.channels[2] = -45;
    assert(engine.stageAnalogSnapshot(analog, 3u));
    assert(engine.commitAnalogSnapshot());
    assert(engine.committedAnalogState().has_value());
    assert(engine.committedAnalogState()->channels[0] == 123);
    assert(engine.committedAnalogState()->channels[2] == -45);
    assert(engine.committedDigitalMask().has_value());
    assert(*engine.committedDigitalMask() == 0x08u);

    engine.notePollFailure();
    engine.applyNeutralFallback(4u);
    assert(engine.stats().pollFailureCount == 1u);
    assert(engine.stats().neutralFallbackCount == 1u);
    assert(engine.committedDigitalMask().has_value());
    assert(*engine.committedDigitalMask() == 0u);
    assert(engine.committedAnalogState().has_value());
    assert(!engine.committedAnalogState()->anyActive());
    assert(engine.stats().lastCommittedGeneration == 4u);

    return 0;
}