#include <cassert>
#include <cstdint>

#include "MemoryStorage.hpp"
#include "MemorySnapshot/MemorySnapshot.hpp"
#include "templ/reg_uint16.impl.hpp"

int main()
{
    using AddressType = uint16_t;
    using DataType = uint8_t;
    using RegType = uint16_t;

    BMMQ::MemoryStorage<AddressType, DataType> store;
    store.addMemBlock(std::make_tuple(
        static_cast<AddressType>(0x0000),
        static_cast<AddressType>(0x0100),
        BMMQ::MEM_READ_WRITE));

    BMMQ::RegisterFile<RegType> mainFile;
    auto* afEntry = mainFile.addRegister("AF", true);
    auto* pcEntry = mainFile.addRegister("PC", false);

    afEntry->second->value = static_cast<RegType>(0x12F0);
    pcEntry->second->value = static_cast<RegType>(0x3456);

    BMMQ::MemorySnapshot<AddressType, DataType, RegType> snapshot(store);
    snapshot.copyRegisterFromMainFile("AF", mainFile);
    snapshot.copyRegisterFromMainFile("PC", mainFile);

    auto* afSnapEntry = snapshot.file.findRegister("AF");
    auto* pcSnapEntry = snapshot.file.findRegister("PC");
    assert(afSnapEntry != nullptr);
    assert(pcSnapEntry != nullptr);

    auto* afMainPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(afEntry->second);
    auto* afSnapPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(afSnapEntry->second);
    assert(afMainPair != nullptr);
    assert(afSnapPair != nullptr);
    assert(afSnapPair->value == afMainPair->value);
    assert(afSnapPair->hi == afMainPair->hi);
    assert(afSnapPair->lo == afMainPair->lo);

    auto* pcMainPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(pcEntry->second);
    auto* pcSnapPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(pcSnapEntry->second);
    assert(pcMainPair == nullptr);
    assert(pcSnapPair == nullptr);
    assert(pcSnapEntry->second->value == pcEntry->second->value);

    afSnapPair->value = static_cast<RegType>(0xABCD);
    pcSnapEntry->second->value = static_cast<RegType>(0x7777);

    assert(afEntry->second->value == static_cast<RegType>(0x12F0));
    assert(pcEntry->second->value == static_cast<RegType>(0x3456));

    return 0;
}
