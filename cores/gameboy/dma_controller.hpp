#pragma once
#include <vector>
#include <cstdint>
#include <string>

using AddressType = uint16_t;
using DataType = uint8_t;

struct DmaEvent {
    AddressType address;
    DataType data;
};

class DmaController {
public:
    void push_event(AddressType address, DataType data);
    bool is_queue_empty() const;
    std::vector<DmaEvent> get_completed_events();
    void clear_queue();

private:
    std::vector<DmaEvent> events_;
};
