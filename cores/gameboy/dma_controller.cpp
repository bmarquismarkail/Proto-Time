#include "dma_controller.hpp"

void DmaController::push_event(AddressType address, DataType data) {
    events_.push_back({address, data});
}

bool DmaController::is_queue_empty() const {
    return events_.empty();
}

std::vector<DmaEvent> DmaController::get_completed_events() {
    std::vector<DmaEvent> completed = std::move(events_);
    events_.clear();
    return completed;
}

void DmaController::clear_queue() {
    events_.clear();
}
