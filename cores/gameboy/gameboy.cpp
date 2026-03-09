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
    regfile.addRegister("mdr", false);
    regfile.addRegister("ime", false);

    return regfile;
}

BMMQ::MemoryPool<AddressType, DataType, AddressType>& LR3592_DMG::getMemory()
{
	return mem;
}

//
BMMQ::fetchBlock<AddressType, DataType> LR3592_DMG::fetch()
{
    // Static instruction stream for smoke-cycle validation: LD A,0x12; NOP
    BMMQ::fetchBlock<AddressType, DataType> f ;
    f.setbaseAddress(cip);

    BMMQ::fetchBlockData<AddressType, DataType> data {
        0, std::vector<DataType> {0x3E, 0x12, 0x00}
    };

    f.getblockData().push_back(data);
    return f;
};

BMMQ::executionBlock<AddressType, DataType, AddressType>
LR3592_DMG::decode(BMMQ::fetchBlock<AddressType, DataType>& fetchData)
{
    BMMQ::executionBlock<AddressType, DataType, AddressType> block;
    block.setSnapshot(&mem);

    for (const auto& dataBlock : fetchData.getblockData()) {
        for (std::size_t i = 0; i < dataBlock.data.size(); ++i) {
            const auto opcode = dataBlock.data[i];

            if (opcode == 0x00) {
                block.addStep([](auto&, auto&) {});
                continue;
            }

            if (opcode == 0x3E && (i + 1) < dataBlock.data.size()) {
                const DataType imm = dataBlock.data[++i];
                block.addStep([imm](auto& snapshot, auto&) {
                    auto* pool = dynamic_cast<BMMQ::MemoryPool<AddressType, DataType, AddressType>*>(&snapshot);
                    if (pool == nullptr) return;

                    auto* afEntry = pool->file.findRegister("AF");
                    if (afEntry == nullptr || afEntry->second == nullptr) return;

                    auto* af = dynamic_cast<LR3592_RegisterPair*>(afEntry->second);
                    if (af == nullptr) return;

                    af->hi = imm;
                });
            }
        }
    }

    return block;
}

void LR3592_DMG::execute(const BMMQ::executionBlock<AddressType, DataType, AddressType>& block, BMMQ::fetchBlock<AddressType, DataType> &fb )
{
    auto* pcEntry = mem.file.findRegister("PC");
    feedback.pcBefore = (pcEntry != nullptr && pcEntry->second != nullptr)
        ? static_cast<uint32_t>(pcEntry->second->value)
        : 0;

    feedback.isControlFlow = false;
    std::size_t executedByteCount = 0;
    for (const auto& dataBlock : fb.getblockData()) {
        executedByteCount += dataBlock.data.size();
        for (const auto opcode : dataBlock.data) {
            if (opcode == 0x00 || opcode == 0xC3 || opcode == 0xCD || opcode == 0xC9) {
                feedback.isControlFlow = true;
            }
        }
    }
    feedback.segmentBoundaryHint = feedback.isControlFlow;

    auto* snapshot = block.getSnapshot();
    if (snapshot == nullptr) return;

    for (const auto& step : block.getSteps()) {
        step(*snapshot, fb);
    }

    if (pcEntry != nullptr && pcEntry->second != nullptr) {
        pcEntry->second->value = static_cast<AddressType>(
            pcEntry->second->value + executedByteCount);
        feedback.pcAfter = static_cast<uint32_t>(pcEntry->second->value);
    } else {
        feedback.pcAfter = feedback.pcBefore;
    }
}

const BMMQ::CpuFeedback& LR3592_DMG::getLastFeedback() const
{
    return feedback;
}

void LR3592_DMG::setStopFlag(bool f){
	stopFlag = f;
}
