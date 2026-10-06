#pragma once
#include "memory/MemorySnapshot/MemorySnapshot.hpp"
#include <memory>
#include <string>
namespace BMMQ::Space {
// One current owned sparse execution-state view per captured CFG block.
// Shared only as immutable captured revisions; new state replaces the view.
struct BlockSnapshot {
    MemoryStorage<uint64_t,uint8_t> backing;
    MemorySnapshot<uint64_t,uint8_t,uint16_t> memory{backing};
    std::string stamp;
    BlockSnapshot()=default;
    BlockSnapshot(const BlockSnapshot&)=delete;
    BlockSnapshot& operator=(const BlockSnapshot&)=delete;
};
}
