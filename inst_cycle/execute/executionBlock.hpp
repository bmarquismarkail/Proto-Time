#ifndef EXECUTION_BLOCK_H
#define EXECUTION_BLOCK_H

#include "../opcode.hpp"
#include "../../memory/IMemory.hpp"

namespace BMMQ
{

    // This is where we will hold blocks of execution
    // NOTE: executionBlocks need to be changed. 
    // because opcodes are fluid, this template can'tb be used.
    template <typename AddressType, typename DataType, typename RegType>
    class executionBlock
    {
    public:
    private:
        //auto code;
        IMemory<AddressType, DataType, RegType> snapshot;
    };
} // namespace BMMQ
#endif