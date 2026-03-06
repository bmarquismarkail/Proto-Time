#ifndef EXECUTION_BLOCK_H
#define EXECUTION_BLOCK_H

#include <functional>
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

        const std::vector<step_t>& getSteps() const {
            return code;
        }

    private:
        std::vector<step_t> code;
        IMemory<AddressType, DataType, RegType>* snapshot = nullptr;
    };
} // namespace BMMQ
#endif
