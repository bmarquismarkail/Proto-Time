#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>

#include "cores/gameboy/gameboy.hpp"
#include "cores/gameboy/dma_controller.hpp"

int main() {
    // Test DmaController class logic directly
    DmaController dma_controller;

    // Assert that the DMA completion queue is initially empty
    assert(dma_controller.is_queue_empty());

    // Simulate a DMA transfer completion event
    dma_controller.push_event(0x8000, 0xFF);

    // Assert that the DMA completion queue now contains the expected event
    assert(!dma_controller.is_queue_empty());
    auto events = dma_controller.get_completed_events();
    assert(events.size() == 1);
    assert(events[0].address == 0x8000);
    assert(events[0].data == 0xFF);

    // Assert that the queue is empty after retrieval
    assert(dma_controller.is_queue_empty());

    std::cout << "DMA completion notification test passed!" << std::endl;
    return 0;
}
