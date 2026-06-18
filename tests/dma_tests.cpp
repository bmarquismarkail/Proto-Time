#include <cassert>
#include <cstdint>
#include <iostream>

#include "cores/gameboy/gameboy.hpp"

namespace {

BMMQ::CpuFeedback step(LR3592_DMG& dmg)
{
    auto fetchBlock = dmg.fetch();
    auto execBlock = dmg.decode(fetchBlock);
    dmg.execute(execBlock, fetchBlock);
    return dmg.getLastFeedback();
}

void startDma(LR3592_DMG& dmg, uint8_t sourceHighByte)
{
    const DataType value[] = {sourceHighByte};
    const bool handled = dmg.handleMemoryWrite(0xFF46, std::span<const DataType>{value, 1});
    assert(handled);
    (void)handled;
}

void test_dma_completion_notification()
{
    LR3592_DMG dmg;

    assert(dmg.getDmaController().is_queue_empty());

    startDma(dmg, 0xC0);

    uint32_t retiredCycles = 0;
    uint32_t iterations = 0;
    const uint32_t MAX_ITERATIONS = 10000;
    while (retiredCycles < 0xA0u * 4u) {
        retiredCycles += step(dmg).retiredCycles;
        if (++iterations > MAX_ITERATIONS) {
            assert(false && "DMA loop timed out");
        }
    }

    assert(!dmg.getDmaController().is_queue_empty());
    auto events = dmg.getDmaController().get_completed_events();
    assert(events.size() == 1);
    assert(events[0].address == 0x8000);
    assert(events[0].data == 0xFF);
    assert(dmg.getDmaController().is_queue_empty());

    std::cout << "DMA completion notification test passed!" << std::endl;
}

} // namespace

int main()
{
    std::cout << "Running DMA completion notification test..." << std::endl;
    test_dma_completion_notification();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
