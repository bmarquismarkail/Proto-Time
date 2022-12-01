#include "gameboy.hpp"

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;


LR3592_DMG::LR3592_DMG()
{
    mem.file = buildRegisterfile();

    AF.registration(mem.file, "AF");
    BC.registration(mem.file, "BC");
    DE.registration(mem.file, "DE");
    HL.registration(mem.file, "HL");
    SP.registration(mem.file, "SP");
    PC.registration(mem.file, "PC");

    mem.store = buildMemoryStore();
//        populateOpcodes();
}

BMMQ::MemoryStorage<AddressType, DataType> LR3592_DMG::buildMemoryStore()
{
    BMMQ::MemoryStorage<AddressType, DataType> store;
    store.addMemBlock(std::make_tuple(0x0000, 0x4000, BMMQ::memAccess::MEM_READ));
    store.addMemBlock(std::make_tuple(0x4000, 0x4000, BMMQ::memAccess::MEM_READ));
    store.addMemBlock(std::make_tuple(0x8000, 0x2000, BMMQ::memAccess::MEM_READ_WRITE));
    store.addMemBlock(std::make_tuple(0xa000, 0x2000, BMMQ::memAccess::MEM_READ_WRITE));
    store.addMemBlock(std::make_tuple(0xc000, 0x2000, BMMQ::memAccess::MEM_READ_WRITE));
    store.addMemBlock(std::make_tuple(0xe000, 0x1e00, BMMQ::memAccess::MEM_UNMAPPED));
    store.addMemBlock(std::make_tuple(0xfe00, 0x00a0, BMMQ::memAccess::MEM_READ_WRITE));
    store.addMemBlock(std::make_tuple(0xfea0, 0x0060, BMMQ::memAccess::MEM_UNMAPPED));
    store.addMemBlock(std::make_tuple(0xff00, 0x004c, BMMQ::memAccess::MEM_READ_WRITE));
    store.addMemBlock(std::make_tuple(0xff4c, 0x0034, BMMQ::memAccess::MEM_UNMAPPED));
    store.addMemBlock(std::make_tuple(0xff80, 0x007f, BMMQ::memAccess::MEM_READ_WRITE));
    store.addMemBlock(std::make_tuple(0xffff, 0x0001, BMMQ::memAccess::MEM_READ_WRITE));
    return store;
}

BMMQ::RegisterFile<AddressType> LR3592_DMG::buildRegisterfile()
{

    BMMQ::RegisterFile<AddressType> regfile;

    regfile.addRegister("AF", true);
    regfile.addRegister("BC", true);
    regfile.addRegister("DE", true);
    regfile.addRegister("HL", true);
    regfile.addRegister("SP", false);
    regfile.addRegister("PC", false);
    //regfile.addRegister // mar
    //regfile.addRegister // mdr

    return regfile;
}

BMMQ::MemoryPool<AddressType, DataType, AddressType>& LR3592_DMG::getMemory()
{
	return mem;
}

//
BMMQ::fetchBlock<AddressType, DataType> LR3592_DMG::fetch()
{
    // building a static fetchblock for testing
    BMMQ::fetchBlock<AddressType, DataType> f ;
    f.setbaseAddress(cip);

    BMMQ::fetchBlockData<AddressType, DataType> data {0, std::vector<DataType> {0x3E} };

    f.getblockData().push_back(data);
    return f;
};

BMMQ::executionBlock<AddressType, DataType, AddressType> LR3592_DMG::decode(BMMQ::OpcodeList<AddressType, DataType, AddressType> &oplist, BMMQ::fetchBlock<AddressType, DataType>& fetchData)
{
    // building a static execution block
    BMMQ::executionBlock<AddressType, DataType, AddressType> b;
    mdr.value = 255;
    auto &fb = fetchData.getblockData();
    for( auto& i : fb ) {
        for (auto data : i.data)
            b.getBlock().push_back(opcodeList[data]);
    }
    return b;
};

void LR3592_DMG::execute(const BMMQ::executionBlock<AddressType, DataType, AddressType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb )
{
    for (auto e : block.getBlock() ) {
        (e)(block.getMemory());
    }
};

void LR3592_DMG::setStopFlag(bool f){
	stopFlag = f;
}