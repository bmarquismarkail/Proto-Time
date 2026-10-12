#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
namespace BMMQ::Space {
enum class Kind : uint8_t { Begin, Fetch, Read, Write, Device, Dma, Mapping, End, Inspection, Gap, Input };
inline uint32_t hardwareCategoryBit(uint64_t location,bool write,std::string_view core="gameboy") noexcept {
    auto space=location>>32;auto a=uint16_t(location);
    if(core=="gamegear") {
        if(space==1&&write)return 1;
        if(space==7){
            if(a==6||((a&0xc0)==0x40&&write))return 256;
            if(a<=5)return a==0?16:32;
            if(a>=7&&a<=0x3f)return (a&1)?16:4096;
            if((a&0xc0)==0x80)return 2048;
            if((a&0xc0)==0x40)return 64;
            if((a&0xfe)==0xdc)return 16;
        }
        if(space>=8&&space<=11)return 2048;
        if(space==4)return 2048;
        if(space==5)return 256;
        if(space==6)return 16;
        return 0;
    }
    if(space==1&&write)return 1;
    if(space==4){if(a>=0x8000&&a<=0x97ff)return 2;
    if(a>=0x9800&&a<=0x9fff)return 4;
    if(a>=0xfe00&&a<=0xfe9f)return 8;}
    if(space!=5&&space!=6)return 0;
    if(a==0xff00)return 16;
    if(a==0xff01||a==0xff02)return 32;
    if(a>=0xff04&&a<=0xff07)return 64;
    if(a==0xff0f||a==0xffff)return 128;
    if(a>=0xff10&&a<=0xff26)return 256;
    if(a>=0xff30&&a<=0xff3f)return 512;
    if(a==0xff46||(a>=0xff51&&a<=0xff55))return 1024;
    if((a>=0xff40&&a<=0xff4b)||(a>=0xff68&&a<=0xff6b))return 2048;
    if(a==0xff4f||a==0xff70||a==0xff50)return 4096;
    return 0;
}
enum class AccessOrigin : uint8_t { Unknown, Cpu, Device, Dma, Boundary, Inspection };
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
    AccessOrigin origin = AccessOrigin::Unknown;
    uint16_t address = 0;
    uint8_t value = 0;
    bool accepted = true;
    bool isWrite = false;
    uint64_t location = 0;
    uint64_t sequence = 0;
    uint32_t cycles = 0;
    uint64_t targetLocation = 0, fallthroughLocation = 0;
    std::array<uint16_t,20> registers{}; // Core-described; GB uses first six.
    std::array<uint8_t,3> bytes{};
    uint32_t length = 0;
    // Fetch payloads are fixed chunks; full instructions are assembled off-lane.
    std::array<uint8_t,64> fetchBytes{};
    uint8_t fetchLength=0;
    uint8_t executionSource = 0; // 0 canonical bus, 1 snapshot, 2 initialization
    uint64_t supplierSequence = 0, supplierBlock = 0, supplierEpoch = 0;
    std::array<uint64_t,32> registerSuppliers{}, registerSupplierBlocks{}, registerSupplierEpochs{};
};
class Capture {
public:
    static constexpr size_t capacity = 8192;
    Capture() : records_(std::make_unique<std::array<Record,capacity>>()) {}
    static uintptr_t laneToken() noexcept { static thread_local uint8_t token; return reinterpret_cast<uintptr_t>(&token); }
    void bindProducer() noexcept { uintptr_t empty=0; owner_.compare_exchange_strong(empty,laneToken()); }
    bool onProducerLane() const noexcept { auto owner=owner_.load(); return owner==0 || owner==laneToken(); }
    // These producer-only fields distinguish direct MMIO writes from retirement updates.
    bool cpuWrite=false;
    uint16_t cpuAddress=0;
    size_t cpuLength=0;
    struct Watch {bool enabled=false,matched=false;uint16_t first=0,last=0;bool write=false;uint32_t categoryMask=0;Record record{};} watch;
    void push(Record record) noexcept {
        if(!onProducerLane())return;
        if(record.origin==AccessOrigin::Unknown){
            if(record.kind==Kind::Dma)record.origin=AccessOrigin::Dma;
            else if(record.kind==Kind::Device)record.origin=cpuWrite&&uint16_t(record.address-cpuAddress)<cpuLength?AccessOrigin::Cpu:AccessOrigin::Device;
            else if(record.kind==Kind::Read||record.kind==Kind::Write||record.kind==Kind::Mapping)
                record.origin=phase==Kind::Read?AccessOrigin::Cpu:phase==Kind::Inspection?AccessOrigin::Inspection:AccessOrigin::Boundary;
            else record.origin=AccessOrigin::Inspection;
        }
        if (stopped_.load(std::memory_order_relaxed)) return;
        if(watch.enabled&&!watch.matched&&record.origin==AccessOrigin::Cpu&&record.accepted&&
           (record.kind==Kind::Read||record.kind==Kind::Write||record.kind==Kind::Device||record.kind==Kind::Mapping)&&
           record.isWrite==watch.write&&record.address>=watch.first&&record.address<=watch.last&&(!watch.categoryMask||(watch.categoryMask & hardwareCategoryBit(record.location,record.isWrite,core)))){watch.matched=true;watch.record=record;}
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
    std::string_view core="gameboy";
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
    uint32_t reads = 0, writes = 0; // A,F,B,C,D,E,H,L,SP,PC
    std::string text;
    std::string flow = "fallthrough";
    bool conditional = false;
    uint16_t target = 0;
    bool direct = false;
    bool taken = false;
};
OperandInfo decode(std::span<const uint8_t> bytes, uint16_t pc, uint16_t after, uint8_t flags = 0);
}
