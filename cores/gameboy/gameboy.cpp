#include "gameboy.hpp"

#include <cassert>
#include <iomanip>
#include <sstream>

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;

namespace {
template <typename Snapshot>
LR3592_RegisterPair* getRegisterPair(Snapshot& snapshot, const char* name) {
    auto* pool = dynamic_cast<BMMQ::MemoryPool<AddressType, DataType, AddressType>*>(&snapshot);
    assert(pool != nullptr && "Expected MemoryPool snapshot");
    if (pool == nullptr) return nullptr;

    auto* entry = pool->file.findRegister(name);
    assert(entry != nullptr && entry->second != nullptr && "Register missing in snapshot");
    if (entry == nullptr || entry->second == nullptr) return nullptr;

    auto* regPair = dynamic_cast<LR3592_RegisterPair*>(entry->second);
    assert(regPair != nullptr && "Register entry is not LR3592_RegisterPair");
    return regPair;
}
}


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
    loadProgram({0x3E, 0x12, 0x00});
    populateOpcodes();
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
    BMMQ::fetchBlock<AddressType, DataType> f ;

    auto* pcEntry = mem.file.findRegister("PC");
    const auto pc = (pcEntry != nullptr && pcEntry->second != nullptr)
        ? static_cast<AddressType>(pcEntry->second->value)
        : static_cast<AddressType>(0);

    f.setbaseAddress(pc);

    std::vector<DataType> stream(3, 0);
    mem.read(stream.data(), pc, static_cast<AddressType>(stream.size()));

    BMMQ::fetchBlockData<AddressType, DataType> data {
        0, std::move(stream)
    };

    f.getblockData().push_back(data);
    return f;
};

void LR3592_DMG::loadProgram(const std::vector<DataType>& program,
                             AddressType startAddress)
{
    auto* destination = mem.store.getPos(startAddress);
    if (destination == nullptr) return;

    for (std::size_t i = 0; i < program.size(); ++i) {
        destination[i] = program[i];
    }
}

BMMQ::executionBlock<AddressType, DataType, AddressType>
LR3592_DMG::decode(BMMQ::fetchBlock<AddressType, DataType>& fetchData)
{
    BMMQ::executionBlock<AddressType, DataType, AddressType> block;
    block.setSnapshot(&mem);

    auto& blockDataList = fetchData.getblockData();
    for (auto& dataBlock : blockDataList) {
        std::size_t i = 0;
        std::size_t consumedBytes = 0;
        while (i < dataBlock.data.size()) {
            const auto opcodeByte = dataBlock.data[i];
            const auto& entry = opcodeTable[opcodeByte];
            if (!entry.has_value()) {
                const AddressType pc = static_cast<AddressType>(
                    fetchData.getbaseAddress() + dataBlock.offset + i);
                std::ostringstream oss;
                oss << "unimplemented opcode 0x"
                    << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(opcodeByte)
                    << " at PC=0x"
                    << std::setw(4) << static_cast<unsigned>(pc);
                throw std::runtime_error(oss.str());
            }

            const auto opcodeLength = entry->length();
            if (opcodeLength == 0) {
                throw std::runtime_error("invalid opcode length");
            }

            if (i + opcodeLength > dataBlock.data.size()) {
                throw std::runtime_error("truncated opcode stream");
            }

            entry->emit(block, dataBlock, i);
            i += opcodeLength;
            consumedBytes += opcodeLength;
        }

        dataBlock.data.resize(consumedBytes);
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

void LR3592_DMG::populateOpcodes()
{
    opcodeTable.fill(std::nullopt);

    opcodeTable[0x00] = BMMQ::make_opcode<AddressType, DataType, AddressType>(
        1,
        BMMQ::make_microcode<AddressType, DataType, AddressType>(
            [](auto&, const auto&, std::size_t) {
                // NOP: intentionally no steps to execute
            }));

    opcodeTable[0x3E] = BMMQ::make_opcode<AddressType, DataType, AddressType>(
        2,
        BMMQ::make_microcode<AddressType, DataType, AddressType>(
            [](auto& block, const auto& fetchData, std::size_t opcodeIndex) {
                const DataType imm = fetchData.data[opcodeIndex + 1];
                block.addStep([imm](auto& snapshot, auto&) {
                    auto* af = getRegisterPair(snapshot, "AF");
                    if (af == nullptr) return;

                    af->hi = imm;
                });
            }));
}
