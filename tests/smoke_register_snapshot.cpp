#include <cassert>
#include <cstdint>

#include "MemoryStorage.hpp"
#include "MemorySnapshot/MemorySnapshot.hpp"
#include "machine/RegisterId.hpp"
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
        BMMQ::memAccess::ReadWrite));

    BMMQ::RegisterFile<RegType> mainFile;
    auto& afEntry = mainFile.addRegister(BMMQ::RegisterId::AF, true);
    auto& pcEntry = mainFile.addRegister(BMMQ::RegisterId::PC, false);

    afEntry.reg->value = static_cast<RegType>(0x12F0);
    pcEntry.reg->value = static_cast<RegType>(0x3456);

    BMMQ::MemorySnapshot<AddressType, DataType, RegType> snapshot(store);
    snapshot.copyRegisterFromMainFile(BMMQ::RegisterId::AF, mainFile);
    snapshot.copyRegisterFromMainFile(BMMQ::RegisterId::PC, mainFile);

    auto* afSnapEntry = snapshot.file.findRegister(BMMQ::RegisterId::AF);
    auto* pcSnapEntry = snapshot.file.findRegister(BMMQ::RegisterId::PC);
    assert(afSnapEntry != nullptr);
    assert(pcSnapEntry != nullptr);

    auto* afMainPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(afEntry.reg.get());
    auto* afSnapPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(afSnapEntry->reg.get());
    assert(afMainPair != nullptr);
    assert(afSnapPair != nullptr);
    assert(afSnapPair->value == afMainPair->value);
    assert(afSnapPair->hi == afMainPair->hi);
    assert(afSnapPair->lo == afMainPair->lo);

    auto* pcMainPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(pcEntry.reg.get());
    auto* pcSnapPair = dynamic_cast<BMMQ::CPU_RegisterPair<RegType>*>(pcSnapEntry->reg.get());
    assert(pcMainPair == nullptr);
    assert(pcSnapPair == nullptr);
    assert(pcSnapEntry->reg->value == pcEntry.reg->value);

    afSnapPair->value = static_cast<RegType>(0xABCD);
    pcSnapEntry->reg->value = static_cast<RegType>(0x7777);

    assert(afEntry.reg->value == static_cast<RegType>(0x12F0));
    assert(pcEntry.reg->value == static_cast<RegType>(0x3456));

    return 0;
}
