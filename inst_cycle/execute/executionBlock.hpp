#ifndef EXECUTION_BLOCK_H
#define EXECUTION_BLOCK_H

#include <functional>
#include <cstddef>
#include <vector>

#include "../../memory/IMemory.hpp"
#include "../fetch/fetchBlock.hpp"

namespace BMMQ
{

    // This is where we will hold blocks of execution
    // NOTE: executionBlocks need to be changed. 
    // because opcodes are fluid, this template can'tb be used.
    template <typename AddressType, typename DataType, typename RegType>
    class executionBlock
    {
    public:
        using step_t = std::function<void(
            IMemory<AddressType, DataType, RegType>&,
            fetchBlock<AddressType, DataType>&)>;

        void setSnapshot(IMemory<AddressType, DataType, RegType>* mem) {
            snapshot = mem;
        }

        IMemory<AddressType, DataType, RegType>* getSnapshot() const {
            return snapshot;
        }

        void addStep(step_t step) {
            code.push_back(std::move(step));
        }

        void addCycleCharge(std::size_t notTakenCycles, std::size_t takenCycles) {
            cyclesIfNotTaken_ += notTakenCycles;
            cyclesIfTaken_ += takenCycles;
        }

        void addCycleCharge(std::size_t cycles) {
            addCycleCharge(cycles, cycles);
        }

        void clear() {
            code.clear();
            cyclesIfNotTaken_ = 0;
            cyclesIfTaken_ = 0;
        }

        void reserve(std::size_t count) {
            code.reserve(count);
        }

        const std::vector<step_t>& getSteps() const {
            return code;
        }

        std::size_t cyclesIfNotTaken() const {
            return cyclesIfNotTaken_;
        }

        std::size_t cyclesIfTaken() const {
            return cyclesIfTaken_;
        }

    private:
        std::vector<step_t> code;
        IMemory<AddressType, DataType, RegType>* snapshot = nullptr;
        std::size_t cyclesIfNotTaken_ = 0;
        std::size_t cyclesIfTaken_ = 0;
    };
} // namespace BMMQ
#endif
