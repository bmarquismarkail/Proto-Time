#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
namespace BMMQ::Space {
enum class Kind : uint8_t { Begin, Fetch, Read, Write, Device, Dma, Mapping, End, Inspection, Gap, Input };
class StateBudget {
public:
    explicit StateBudget(size_t maximum=64u*1024u*1024u):maximum(maximum){}
    bool reserve(size_t charge) noexcept {auto old=used.load();while(charge<=maximum-std::min(maximum,old)){if(used.compare_exchange_weak(old,old+charge))return true;}return false;}
    void release(size_t charge) noexcept {used.fetch_sub(charge);}
    std::atomic<size_t> used{0};
    size_t maximum;
};
// Fixed records: callbacks never allocate, lock, perform I/O, or inspect the bus.
struct Record {
    Kind kind{};
    uint16_t address = 0;
    uint8_t value = 0;
    bool accepted = true;
    bool isWrite = false;
    uint64_t location = 0;
    uint64_t sequence = 0;
    uint32_t cycles = 0;
    uint64_t targetLocation = 0, fallthroughLocation = 0;
    std::array<uint16_t,6> registers{}; // AF, BC, DE, HL, SP, PC
    std::array<uint8_t,3> bytes{};
    uint8_t length = 0;
    uint8_t executionSource = 0; // 0 canonical bus, 1 snapshot, 2 initialization
    uint64_t supplierSequence = 0, supplierBlock = 0, supplierEpoch = 0;
    std::array<uint64_t,10> registerSuppliers{}, registerSupplierBlocks{}, registerSupplierEpochs{};
};
class Capture {
public:
    static constexpr size_t capacity = 8192;
    Capture() : records_(std::make_unique<std::array<Record,capacity>>()) {}
    static uintptr_t laneToken() noexcept { static thread_local uint8_t token; return reinterpret_cast<uintptr_t>(&token); }
    void bindProducer() noexcept { uintptr_t empty=0; owner_.compare_exchange_strong(empty,laneToken()); }
    bool onProducerLane() const noexcept { auto owner=owner_.load(); return owner==0 || owner==laneToken(); }
    void push(Record record) noexcept {
        if (!onProducerLane()) return;
        if (stopped_.load(std::memory_order_relaxed)) return;
        const auto w = written_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == capacity) {
            lost_.fetch_add(1, std::memory_order_relaxed);
            stopped_.store(true, std::memory_order_release);
            return;
        }
        (*records_)[w % capacity] = record;
        written_.store(w+1,std::memory_order_release);
    }
    bool pop(Record& record) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == written_.load(std::memory_order_acquire)) return false;
        record = (*records_)[r % capacity];
        read_.store(r+1,std::memory_order_release);
        return true;
    }
    uint64_t pending() const noexcept { return written_.load()-read_.load(); }
    uint64_t lost() const noexcept { return lost_.load(); }
    void stop() noexcept { stopped_.store(true); }
    bool stopped() const noexcept { return stopped_.load(); }
    std::shared_ptr<StateBudget> budget=std::make_shared<StateBudget>();
    Kind phase = Kind::Inspection; // producer lane only
    uint64_t sequence = 0;
private:
    std::unique_ptr<std::array<Record,capacity>> records_;
    std::atomic<uintptr_t> owner_{0};
    std::atomic<uint64_t> written_{0}, read_{0}, lost_{0};
    std::atomic<bool> stopped_{false};
};
// Address-space identity is independent of logical aliases and mapping epochs.
inline uint64_t location(uint8_t space, uint16_t bank, uint16_t offset) noexcept {
    return (uint64_t(space)<<32) | (uint64_t(bank)<<16) | offset;
}
std::string digest(std::span<const uint8_t> bytes);
struct OperandInfo {
    uint16_t reads = 0, writes = 0; // A,F,B,C,D,E,H,L,SP,PC
    std::string text;
    std::string flow = "fallthrough";
    bool conditional = false;
    uint16_t target = 0;
    bool direct = false;
    bool taken = false;
};
OperandInfo decode(std::span<const uint8_t> bytes, uint16_t pc, uint16_t after, uint8_t flags = 0);
}
