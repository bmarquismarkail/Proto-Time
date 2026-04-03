#include "gameboy.hpp"

#include "decode/gb_opcode_decode.hpp"
#include "hardware_registers.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <sstream>

using AddressType = uint16_t;
using DataType = uint8_t;
using LR3592_Register = BMMQ::CPU_Register<AddressType>;
using LR3592_RegisterPair = BMMQ::CPU_RegisterPair<AddressType>;

namespace {

constexpr DataType kFlagZ = 0x80;
constexpr DataType kFlagN = 0x40;
constexpr DataType kFlagH = 0x20;
constexpr DataType kFlagC = 0x10;

using MemoryView = BMMQ::IMemory<AddressType, DataType, AddressType>;
using MemoryPool = BMMQ::MemoryPool<AddressType, DataType, AddressType>;

MemoryPool* getMemoryPool(MemoryView& snapshot)
{
    auto* pool = dynamic_cast<MemoryPool*>(&snapshot);
    assert(pool != nullptr && "Expected MemoryPool snapshot");
    return pool;
}

LR3592_Register* getRegister(MemoryView& snapshot, BMMQ::RegisterId id)
{
    auto* pool = getMemoryPool(snapshot);
    auto* entry = pool->file.findRegister(id);
    assert(entry != nullptr && entry->reg != nullptr && "Register missing in snapshot");
    return entry != nullptr ? entry->reg.get() : nullptr;
}

LR3592_Register* getRegister(MemoryView& snapshot, const char* name)
{
    auto* pool = getMemoryPool(snapshot);
    auto* entry = pool->file.findRegister(name);
    assert(entry != nullptr && entry->reg != nullptr && "Register missing in snapshot");
    return entry != nullptr ? entry->reg.get() : nullptr;
}

template <typename Snapshot>
LR3592_RegisterPair* getRegisterPair(Snapshot& snapshot, BMMQ::RegisterId id)
{
    auto* pool = dynamic_cast<MemoryPool*>(&snapshot);
    assert(pool != nullptr && "Expected MemoryPool snapshot");
    if (pool == nullptr) return nullptr;

    auto* entry = pool->file.findRegister(id);
    assert(entry != nullptr && entry->reg != nullptr && "Register missing in snapshot");
    if (entry == nullptr || entry->reg == nullptr) return nullptr;

    auto* regPair = dynamic_cast<LR3592_RegisterPair*>(entry->reg.get());
    assert(regPair != nullptr && "Register entry is not LR3592_RegisterPair");
    return regPair;
}

template <typename Snapshot>
LR3592_RegisterPair* getRegisterPair(Snapshot& snapshot, const char* name)
{
    auto* pool = dynamic_cast<MemoryPool*>(&snapshot);
    assert(pool != nullptr && "Expected MemoryPool snapshot");
    if (pool == nullptr) return nullptr;

    auto* entry = pool->file.findRegister(name);
    assert(entry != nullptr && entry->reg != nullptr && "Register missing in snapshot");
    if (entry == nullptr || entry->reg == nullptr) return nullptr;

    auto* regPair = dynamic_cast<LR3592_RegisterPair*>(entry->reg.get());
    assert(regPair != nullptr && "Register entry is not LR3592_RegisterPair");
    return regPair;
}

DataType read8(MemoryView& snapshot, AddressType address)
{
    DataType value = 0;
    snapshot.read(std::span<DataType>(&value, 1), address);
    return value;
}

AddressType read16(MemoryView& snapshot, AddressType address)
{
    std::array<DataType, 2> bytes {0, 0};
    snapshot.read(std::span<DataType>(bytes.data(), bytes.size()), address);
    return static_cast<AddressType>(bytes[0] | (static_cast<AddressType>(bytes[1]) << 8));
}

void write8(MemoryView& snapshot, AddressType address, DataType value)
{
    std::array<DataType, 1> bytes {value};
    snapshot.write(std::span<const DataType>(bytes.data(), bytes.size()), address);
}

void write16(MemoryView& snapshot, AddressType address, AddressType value)
{
    std::array<DataType, 2> bytes {
        static_cast<DataType>(value & 0x00FFu),
        static_cast<DataType>((value >> 8) & 0x00FFu)
    };
    snapshot.write(std::span<const DataType>(bytes.data(), bytes.size()), address);
}

DataType fetchImm8(const BMMQ::fetchBlockData<AddressType, DataType>& fetchData, std::size_t opcodeIndex)
{
    return fetchData.data[opcodeIndex + 1];
}

AddressType fetchImm16(const BMMQ::fetchBlockData<AddressType, DataType>& fetchData, std::size_t opcodeIndex)
{
    return static_cast<AddressType>(
        fetchData.data[opcodeIndex + 1] |
        (static_cast<AddressType>(fetchData.data[opcodeIndex + 2]) << 8));
}

AddressType& accessR16(MemoryView& snapshot, GB::Decode::R16 reg)
{
    switch (reg) {
    case GB::Decode::R16::BC:
        return getRegisterPair(snapshot, GB::RegisterId::BC)->value;
    case GB::Decode::R16::DE:
        return getRegisterPair(snapshot, GB::RegisterId::DE)->value;
    case GB::Decode::R16::HL:
        return getRegisterPair(snapshot, GB::RegisterId::HL)->value;
    case GB::Decode::R16::SP:
        return getRegister(snapshot, GB::RegisterId::SP)->value;
    }

    assert(false && "Invalid r16");
    return getRegister(snapshot, GB::RegisterId::SP)->value;
}

AddressType& accessStackR16(MemoryView& snapshot, GB::Decode::R16Stack reg)
{
    switch (reg) {
    case GB::Decode::R16Stack::BC:
        return getRegisterPair(snapshot, GB::RegisterId::BC)->value;
    case GB::Decode::R16Stack::DE:
        return getRegisterPair(snapshot, GB::RegisterId::DE)->value;
    case GB::Decode::R16Stack::HL:
        return getRegisterPair(snapshot, GB::RegisterId::HL)->value;
    case GB::Decode::R16Stack::AF:
        return getRegisterPair(snapshot, GB::RegisterId::AF)->value;
    }

    assert(false && "Invalid stack r16");
    return getRegisterPair(snapshot, GB::RegisterId::AF)->value;
}

DataType readR8(MemoryView& snapshot, GB::Decode::R8 reg)
{
    switch (reg) {
    case GB::Decode::R8::B:
        return getRegisterPair(snapshot, GB::RegisterId::BC)->hi;
    case GB::Decode::R8::C:
        return getRegisterPair(snapshot, GB::RegisterId::BC)->lo;
    case GB::Decode::R8::D:
        return getRegisterPair(snapshot, GB::RegisterId::DE)->hi;
    case GB::Decode::R8::E:
        return getRegisterPair(snapshot, GB::RegisterId::DE)->lo;
    case GB::Decode::R8::H:
        return getRegisterPair(snapshot, GB::RegisterId::HL)->hi;
    case GB::Decode::R8::L:
        return getRegisterPair(snapshot, GB::RegisterId::HL)->lo;
    case GB::Decode::R8::HLIndirect:
        return read8(snapshot, getRegisterPair(snapshot, GB::RegisterId::HL)->value);
    case GB::Decode::R8::A:
        return getRegisterPair(snapshot, GB::RegisterId::AF)->hi;
    }

    assert(false && "Invalid r8");
    return 0;
}

void writeR8(MemoryView& snapshot, GB::Decode::R8 reg, DataType value)
{
    switch (reg) {
    case GB::Decode::R8::B:
        getRegisterPair(snapshot, GB::RegisterId::BC)->hi = value;
        return;
    case GB::Decode::R8::C:
        getRegisterPair(snapshot, GB::RegisterId::BC)->lo = value;
        return;
    case GB::Decode::R8::D:
        getRegisterPair(snapshot, GB::RegisterId::DE)->hi = value;
        return;
    case GB::Decode::R8::E:
        getRegisterPair(snapshot, GB::RegisterId::DE)->lo = value;
        return;
    case GB::Decode::R8::H:
        getRegisterPair(snapshot, GB::RegisterId::HL)->hi = value;
        return;
    case GB::Decode::R8::L:
        getRegisterPair(snapshot, GB::RegisterId::HL)->lo = value;
        return;
    case GB::Decode::R8::HLIndirect:
        write8(snapshot, getRegisterPair(snapshot, GB::RegisterId::HL)->value, value);
        return;
    case GB::Decode::R8::A:
        getRegisterPair(snapshot, GB::RegisterId::AF)->hi = value;
        return;
    }

    assert(false && "Invalid r8");
}

BMMQ::RegisterByteRef& flags(MemoryView& snapshot)
{
    return getRegisterPair(snapshot, GB::RegisterId::AF)->lo;
}

BMMQ::RegisterByteRef& accumulator(MemoryView& snapshot)
{
    return getRegisterPair(snapshot, GB::RegisterId::AF)->hi;
}

bool conditionHolds(MemoryView& snapshot, GB::Decode::Condition condition)
{
    const DataType flagReg = flags(snapshot);
    switch (condition) {
    case GB::Decode::Condition::NZ:
        return (flagReg & kFlagZ) == 0;
    case GB::Decode::Condition::Z:
        return (flagReg & kFlagZ) != 0;
    case GB::Decode::Condition::NC:
        return (flagReg & kFlagC) == 0;
    case GB::Decode::Condition::C:
        return (flagReg & kFlagC) != 0;
    }

    assert(false && "Invalid condition");
    return false;
}

void setFlags(MemoryView& snapshot, bool z, bool n, bool h, bool c)
{
    flags(snapshot) = static_cast<DataType>(
        (z ? kFlagZ : 0) |
        (n ? kFlagN : 0) |
        (h ? kFlagH : 0) |
        (c ? kFlagC : 0));
}

void updateControlFlowPc(MemoryView& snapshot, AddressType target, DataType instructionLength)
{
    auto* pc = getRegister(snapshot, GB::RegisterId::PC);
    pc->value = static_cast<AddressType>(target - instructionLength);
}

AddressType pop16(MemoryView& snapshot)
{
    auto* sp = getRegister(snapshot, GB::RegisterId::SP);
    const AddressType value = read16(snapshot, static_cast<AddressType>(sp->value));
    sp->value = static_cast<AddressType>(sp->value + 2);
    return value;
}

void push16(MemoryView& snapshot, AddressType value)
{
    auto* sp = getRegister(snapshot, GB::RegisterId::SP);
    sp->value = static_cast<AddressType>(sp->value - 2);
    write16(snapshot, static_cast<AddressType>(sp->value), value);
}

void executeCbOpcode(MemoryView& snapshot, DataType opcode)
{
    const auto reg = GB::Decode::decodeR8(opcode);
    DataType value = readR8(snapshot, reg);
    const DataType oldValue = value;
    const bool carryIn = (flags(snapshot) & kFlagC) != 0;

    switch (opcode >> 6) {
    case 0: {
        switch ((opcode >> 3) & 0x07u) {
        case 0:
            value = static_cast<DataType>((value << 1) | (value >> 7));
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x80u) != 0);
            break;
        case 1:
            value = static_cast<DataType>((value >> 1) | (value << 7));
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x01u) != 0);
            break;
        case 2:
            value = static_cast<DataType>((value << 1) | (carryIn ? 1 : 0));
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x80u) != 0);
            break;
        case 3:
            value = static_cast<DataType>((value >> 1) | (carryIn ? 0x80u : 0u));
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x01u) != 0);
            break;
        case 4:
            value = static_cast<DataType>(value << 1);
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x80u) != 0);
            break;
        case 5:
            value = static_cast<DataType>((value >> 1) | (oldValue & 0x80u));
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x01u) != 0);
            break;
        case 6:
            value = static_cast<DataType>((value >> 4) | (value << 4));
            setFlags(snapshot, value == 0, false, false, false);
            break;
        case 7:
            value = static_cast<DataType>(value >> 1);
            setFlags(snapshot, value == 0, false, false, (oldValue & 0x01u) != 0);
            break;
        }
        writeR8(snapshot, reg, value);
        return;
    }
    case 1: {
        const DataType bit = static_cast<DataType>((opcode >> 3) & 0x07u);
        const bool zero = (value & static_cast<DataType>(1u << bit)) == 0;
        setFlags(snapshot, zero, false, true, (flags(snapshot) & kFlagC) != 0);
        return;
    }
    case 2:
        value = static_cast<DataType>(value & ~(1u << ((opcode >> 3) & 0x07u)));
        writeR8(snapshot, reg, value);
        return;
    case 3:
        value = static_cast<DataType>(value | (1u << ((opcode >> 3) & 0x07u)));
        writeR8(snapshot, reg, value);
        return;
    }
}

bool isControlFlowOpcode(DataType opcode)
{
    switch (opcode) {
    case 0x18:
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38:
    case 0xC0:
    case 0xC2:
    case 0xC3:
    case 0xC4:
    case 0xC7:
    case 0xC8:
    case 0xC9:
    case 0xCA:
    case 0xCC:
    case 0xCD:
    case 0xCF:
    case 0xD0:
    case 0xD2:
    case 0xD4:
    case 0xD7:
    case 0xD8:
    case 0xD9:
    case 0xDA:
    case 0xDC:
    case 0xDF:
    case 0xE7:
    case 0xE9:
    case 0xEF:
    case 0xF7:
    case 0xFF:
        return true;
    default:
        return false;
    }
}

template <typename Emit>
auto makeOpcode(DataType length, Emit&& emit)
{
    return BMMQ::make_opcode<AddressType, DataType, AddressType>(
        length,
        BMMQ::make_microcode<AddressType, DataType, AddressType>(std::forward<Emit>(emit)));
}

template <typename Step>
auto emitStep(DataType length, Step&& step)
{
    return makeOpcode(length, [fn = std::forward<Step>(step)](auto& block, const auto& fetchData, std::size_t opcodeIndex) {
        const DataType opcode = fetchData.data[opcodeIndex];
        fn(block, fetchData, opcodeIndex, opcode);
    });
}

} // namespace

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

void LR3592_DMG::attachMemory(BMMQ::MemoryStorage<AddressType, DataType>& store)
{
    mem.attachStore(store);
}

BMMQ::MemoryStorage<AddressType, DataType> LR3592_DMG::buildMemoryStore()
{
    BMMQ::MemoryStorage<AddressType, DataType> store;
    store.addMemBlock(std::make_tuple(0x0000, 0x4000, BMMQ::memAccess::Read));
    store.addMemBlock(std::make_tuple(0x4000, 0x4000, BMMQ::memAccess::Read));
    store.addMemBlock(std::make_tuple(0x8000, 0x2000, BMMQ::memAccess::ReadWrite));
    store.addMemBlock(std::make_tuple(0xa000, 0x2000, BMMQ::memAccess::ReadWrite));
    store.addMemBlock(std::make_tuple(0xc000, 0x2000, BMMQ::memAccess::ReadWrite));
    store.addMemBlock(std::make_tuple(0xe000, 0x1e00, BMMQ::memAccess::Unmapped));
    store.addMemBlock(std::make_tuple(0xfe00, 0x00a0, BMMQ::memAccess::ReadWrite));
    store.addMemBlock(std::make_tuple(0xfea0, 0x0060, BMMQ::memAccess::Unmapped));
    store.addMemBlock(std::make_tuple(0xff00, 0x004c, BMMQ::memAccess::ReadWrite));
    store.addMemBlock(std::make_tuple(0xff4c, 0x0034, BMMQ::memAccess::Unmapped));
    store.addMemBlock(std::make_tuple(0xff80, 0x007f, BMMQ::memAccess::ReadWrite));
    store.addMemBlock(std::make_tuple(0xffff, 0x0001, BMMQ::memAccess::ReadWrite));
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
    GB::HardwareRegisters::registerIn(regfile);

    return regfile;
}

BMMQ::MemoryPool<AddressType, DataType, AddressType>& LR3592_DMG::getMemory()
{
    return mem;
}

const BMMQ::MemoryPool<AddressType, DataType, AddressType>& LR3592_DMG::getMemory() const
{
    return mem;
}

BMMQ::fetchBlock<AddressType, DataType> LR3592_DMG::fetch()
{
    if (stopFlag || haltFlag) {
        return {};
    }

    BMMQ::fetchBlock<AddressType, DataType> f;

    auto* pcEntry = mem.file.findRegister("PC");
    const auto pc = (pcEntry != nullptr && pcEntry->reg != nullptr)
        ? static_cast<AddressType>(pcEntry->reg->value)
        : static_cast<AddressType>(0);

    f.setbaseAddress(pc);

    std::vector<DataType> stream(1, 0);
    mem.read(std::span<DataType>(stream.data(), stream.size()), pc);

    const auto opcodeByte = stream[0];
    if (const auto& entry = opcodeTable[opcodeByte]; entry.has_value()) {
        const auto opcodeLength = entry->length();
        if (opcodeLength > stream.size()) {
            stream.resize(opcodeLength, 0);
            mem.read(std::span<DataType>(stream.data(), stream.size()), pc);
        }
    }

    BMMQ::fetchBlockData<AddressType, DataType> data {
        0, std::move(stream)
    };

    f.getblockData().push_back(data);
    return f;
};

void LR3592_DMG::loadProgram(const std::vector<DataType>& program,
                             AddressType startAddress)
{
    mem.backingStore().load(std::span<const DataType>(program.data(), program.size()), startAddress);
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

            if (isControlFlowOpcode(opcodeByte)) {
                break;
            }
        }

        dataBlock.data.resize(consumedBytes);
    }

    return block;
}

void LR3592_DMG::execute(const BMMQ::executionBlock<AddressType, DataType, AddressType>& block, BMMQ::fetchBlock<AddressType, DataType>& fb)
{
    auto* pcEntry = mem.file.findRegister("PC");
    feedback.pcBefore = (pcEntry != nullptr && pcEntry->reg != nullptr)
        ? static_cast<uint32_t>(pcEntry->reg->value)
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

    if (pcEntry != nullptr && pcEntry->reg != nullptr) {
        pcEntry->reg->value = static_cast<AddressType>(
            pcEntry->reg->value + executedByteCount);
        feedback.pcAfter = static_cast<uint32_t>(pcEntry->reg->value);
    } else {
        feedback.pcAfter = feedback.pcBefore;
    }
}

const BMMQ::CpuFeedback& LR3592_DMG::getLastFeedback() const
{
    return feedback;
}

void LR3592_DMG::setStopFlag(bool f)
{
    stopFlag = f;
}

void LR3592_DMG::setHaltFlag(bool f)
{
    haltFlag = f;
}

void LR3592_DMG::clearHaltFlag()
{
    haltFlag = false;
}

void LR3592_DMG::populateOpcodes()
{
    opcodeTable.fill(std::nullopt);

    auto setOpcode = [this](DataType opcode, auto&& value) {
        opcodeTable[opcode] = std::forward<decltype(value)>(value);
    };

    setOpcode(0x00, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto&, auto&) {
        });
    }));

    setOpcode(0x10, emitStep(2, [this](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([this](auto&, auto&) {
            setStopFlag(true);
        });
    }));

    setOpcode(0x76, emitStep(1, [this](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([this](auto&, auto&) {
            setHaltFlag(true);
        });
    }));

    for (DataType opcode : {0x01, 0x11, 0x21, 0x31}) {
        setOpcode(opcode, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType code) {
            const AddressType imm = fetchImm16(fetchData, opcodeIndex);
            block.addStep([code, imm](auto& snapshot, auto&) {
                accessR16(snapshot, GB::Decode::decodeR16(code)) = imm;
            });
        }));
    }

    for (DataType opcode : {0x03, 0x13, 0x23, 0x33}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                auto& reg = accessR16(snapshot, GB::Decode::decodeR16(code));
                reg = static_cast<AddressType>(reg + 1);
            });
        }));
    }

    for (DataType opcode : {0x0B, 0x1B, 0x2B, 0x3B}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                auto& reg = accessR16(snapshot, GB::Decode::decodeR16(code));
                reg = static_cast<AddressType>(reg - 1);
            });
        }));
    }

    setOpcode(0x08, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType address = fetchImm16(fetchData, opcodeIndex);
        block.addStep([address](auto& snapshot, auto&) {
            write16(snapshot, address, static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::SP)->value));
        });
    }));

    for (DataType opcode : {0x09, 0x19, 0x29, 0x39}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                auto& hl = getRegisterPair(snapshot, GB::RegisterId::HL)->value;
                const AddressType lhs = hl;
                const AddressType rhs = accessR16(snapshot, GB::Decode::decodeR16(code));
                const AddressType result = static_cast<AddressType>(lhs + rhs);

                const bool h = ((lhs & 0x0FFFu) + (rhs & 0x0FFFu)) > 0x0FFFu;
                const bool c = static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs) > 0xFFFFu;
                const bool z = (flags(snapshot) & kFlagZ) != 0;
                setFlags(snapshot, z, false, h, c);

                hl = result;
            });
        }));
    }

    for (DataType opcode : {0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                const auto reg = GB::Decode::decodeR8Dest(code);
                const DataType oldValue = readR8(snapshot, reg);
                const DataType newValue = static_cast<DataType>(oldValue + 1);
                writeR8(snapshot, reg, newValue);

                const bool h = ((oldValue & 0x0Fu) + 1u) > 0x0Fu;
                const bool z = newValue == 0;
                const bool c = (flags(snapshot) & kFlagC) != 0;
                setFlags(snapshot, z, false, h, c);
            });
        }));
    }

    for (DataType opcode : {0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                const auto reg = GB::Decode::decodeR8Dest(code);
                const DataType oldValue = readR8(snapshot, reg);
                const DataType newValue = static_cast<DataType>(oldValue - 1);
                writeR8(snapshot, reg, newValue);

                const bool h = (oldValue & 0x0Fu) == 0;
                const bool z = newValue == 0;
                const bool c = (flags(snapshot) & kFlagC) != 0;
                setFlags(snapshot, z, true, h, c);
            });
        }));
    }

    for (DataType opcode : {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E}) {
        setOpcode(opcode, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType code) {
            const DataType imm = fetchImm8(fetchData, opcodeIndex);
            block.addStep([code, imm](auto& snapshot, auto&) {
                writeR8(snapshot, GB::Decode::decodeR8Dest(code), imm);
            });
        }));
    }

    for (DataType opcode : {0x07, 0x0F, 0x17, 0x1F}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                auto& a = accumulator(snapshot);
                const bool carryIn = (flags(snapshot) & kFlagC) != 0;
                const DataType oldValue = a;

                switch ((code >> 3) & 0x03u) {
                case 0:
                    a = static_cast<DataType>((a << 1) | (a >> 7));
                    setFlags(snapshot, false, false, false, (oldValue & 0x80u) != 0);
                    break;
                case 1:
                    a = static_cast<DataType>((a >> 1) | (a << 7));
                    setFlags(snapshot, false, false, false, (oldValue & 0x01u) != 0);
                    break;
                case 2:
                    a = static_cast<DataType>((a << 1) | (carryIn ? 1 : 0));
                    setFlags(snapshot, false, false, false, (oldValue & 0x80u) != 0);
                    break;
                case 3:
                    a = static_cast<DataType>((a >> 1) | (carryIn ? 0x80u : 0u));
                    setFlags(snapshot, false, false, false, (oldValue & 0x01u) != 0);
                    break;
                }
            });
        }));
    }

    setOpcode(0x27, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            auto& a = accumulator(snapshot);
            const DataType flagReg = flags(snapshot);
            const bool n = (flagReg & kFlagN) != 0;
            const bool h = (flagReg & kFlagH) != 0;
            const bool c = (flagReg & kFlagC) != 0;

            DataType adjust = 0;
            bool carry = c;

            if (!n) {
                if (h || (a & 0x0Fu) > 0x09u) {
                    adjust |= 0x06u;
                }
                if (c || a > 0x99u) {
                    adjust |= 0x60u;
                    carry = true;
                }
                a = static_cast<DataType>(a + adjust);
            } else {
                if (h) {
                    adjust |= 0x06u;
                }
                if (c) {
                    adjust |= 0x60u;
                }
                a = static_cast<DataType>(a - adjust);
            }

            const bool z = (a == 0);
            flags(snapshot) = static_cast<DataType>(
                (z ? kFlagZ : 0) |
                (n ? kFlagN : 0) |
                (carry ? kFlagC : 0));
        });
    }));

    setOpcode(0x2F, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            accumulator(snapshot) = static_cast<DataType>(~accumulator(snapshot));
            flags(snapshot) = static_cast<DataType>((flags(snapshot) & (kFlagZ | kFlagC)) | kFlagN | kFlagH);
        });
    }));

    setOpcode(0x37, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            flags(snapshot) = static_cast<DataType>(flags(snapshot) & kFlagZ);
            flags(snapshot) = static_cast<DataType>(flags(snapshot) | kFlagC);
        });
    }));

    setOpcode(0x3F, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            const bool carry = (flags(snapshot) & kFlagC) == 0;
            flags(snapshot) = static_cast<DataType>(flags(snapshot) & kFlagZ);
            if (carry) flags(snapshot) = static_cast<DataType>(flags(snapshot) | kFlagC);
        });
    }));

    setOpcode(0x18, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const int8_t offset = static_cast<int8_t>(fetchImm8(fetchData, opcodeIndex));
        block.addStep([offset](auto& snapshot, auto&) {
            const AddressType pc = static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::PC)->value);
            const auto target = static_cast<AddressType>(static_cast<int32_t>(pc) + 2 + offset);
            updateControlFlowPc(snapshot, target, 2);
        });
    }));

    for (DataType opcode : {0x20, 0x28, 0x30, 0x38}) {
        setOpcode(opcode, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType code) {
            const int8_t offset = static_cast<int8_t>(fetchImm8(fetchData, opcodeIndex));
            block.addStep([code, offset](auto& snapshot, auto&) {
                if (!conditionHolds(snapshot, GB::Decode::decodeCondition(code))) return;
                const AddressType pc = static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::PC)->value);
                const auto target = static_cast<AddressType>(static_cast<int32_t>(pc) + 2 + offset);
                updateControlFlowPc(snapshot, target, 2);
            });
        }));
    }

    for (DataType opcode = 0x40; opcode <= 0x7F; ++opcode) {
        if (opcode == 0x76) continue;
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                const auto src = GB::Decode::decodeR8Src(code);
                const auto dest = GB::Decode::decodeR8Dest(code);
                writeR8(snapshot, dest, readR8(snapshot, src));
            });
        }));
    }

    for (DataType opcode : {0x02, 0x12, 0x22, 0x32, 0x0A, 0x1A, 0x2A, 0x3A}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                auto* hl = getRegisterPair(snapshot, GB::RegisterId::HL);
                AddressType address = 0;

                switch (code) {
                case 0x02:
                case 0x0A:
                    address = getRegisterPair(snapshot, GB::RegisterId::BC)->value;
                    break;
                case 0x12:
                case 0x1A:
                    address = getRegisterPair(snapshot, GB::RegisterId::DE)->value;
                    break;
                case 0x22:
                case 0x2A:
                case 0x32:
                case 0x3A:
                    address = hl->value;
                    break;
                }

                if ((code & 0x08u) == 0) {
                    write8(snapshot, address, accumulator(snapshot));
                } else {
                    accumulator(snapshot) = read8(snapshot, address);
                }

                if (code == 0x22 || code == 0x2A) {
                    hl->value = static_cast<AddressType>(hl->value + 1);
                } else if (code == 0x32 || code == 0x3A) {
                    hl->value = static_cast<AddressType>(hl->value - 1);
                }
            });
        }));
    }

    auto addMathOp = [this, &setOpcode](DataType opcode, DataType length, auto readValue) {
        setOpcode(opcode, emitStep(length, [readValue](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType code) {
            const auto operand = readValue(fetchData, opcodeIndex, code);
            block.addStep([code, operand](auto& snapshot, auto&) {
                auto& a = accumulator(snapshot);
                const DataType rhs = operand(snapshot);
                const DataType lhs = a;
                const bool carryIn = (flags(snapshot) & kFlagC) != 0;

                DataType result = lhs;
                bool z = false;
                bool n = false;
                bool h = false;
                bool c = false;

                switch ((code >> 3) & 0x07u) {
                case 0: { // ADD A,r8
                    result = static_cast<DataType>(lhs + rhs);
                    h = static_cast<uint16_t>((lhs & 0x0Fu) + (rhs & 0x0Fu)) > 0x0Fu;
                    c = static_cast<uint16_t>(lhs) + static_cast<uint16_t>(rhs) > 0xFFu;
                    n = false;
                    break;
                }
                case 1: { // ADC A,r8
                    const uint16_t sum = static_cast<uint16_t>(lhs) + static_cast<uint16_t>(rhs) + (carryIn ? 1u : 0u);
                    result = static_cast<DataType>(sum);
                    h = static_cast<uint16_t>((lhs & 0x0Fu) + (rhs & 0x0Fu) + (carryIn ? 1u : 0u)) > 0x0Fu;
                    c = sum > 0xFFu;
                    n = false;
                    break;
                }
                case 2: { // SUB A,r8
                    result = static_cast<DataType>(lhs - rhs);
                    h = (lhs & 0x0Fu) < (rhs & 0x0Fu);
                    c = lhs < rhs;
                    n = true;
                    break;
                }
                case 3: { // SBC A,r8
                    const uint16_t subtrahend = static_cast<uint16_t>(rhs) + (carryIn ? 1u : 0u);
                    result = static_cast<DataType>(lhs - subtrahend);
                    h = (lhs & 0x0Fu) < ((rhs & 0x0Fu) + (carryIn ? 1u : 0u));
                    c = static_cast<uint16_t>(lhs) < subtrahend;
                    n = true;
                    break;
                }
                case 4: { // AND
                    result = static_cast<DataType>(lhs & rhs);
                    n = false;
                    h = true;
                    c = false;
                    break;
                }
                case 5: { // XOR
                    result = static_cast<DataType>(lhs ^ rhs);
                    n = false;
                    h = false;
                    c = false;
                    break;
                }
                case 6: { // OR
                    result = static_cast<DataType>(lhs | rhs);
                    n = false;
                    h = false;
                    c = false;
                    break;
                }
                case 7: { // CP
                    h = (lhs & 0x0Fu) < (rhs & 0x0Fu);
                    c = lhs < rhs;
                    z = lhs == rhs;
                    n = true;
                    setFlags(snapshot, z, n, h, c);
                    return;
                }
                }

                a = result;
                z = (a == 0);
                setFlags(snapshot, z, n, h, c);
            });
        }));
    };

    for (DataType opcode = 0x80; opcode <= 0xBF; ++opcode) {
        addMathOp(opcode, 1, [](const auto&, std::size_t, DataType code) {
            const auto reg = GB::Decode::decodeR8Src(code);
            return [reg](auto& snapshot) {
                return readR8(snapshot, reg);
            };
        });
    }

    for (DataType opcode : {0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE}) {
        addMathOp(opcode, 2, [](const auto& fetchData, std::size_t opcodeIndex, DataType) {
            const DataType imm = fetchImm8(fetchData, opcodeIndex);
            return [imm](auto&) {
                return imm;
            };
        });
    }

    for (DataType opcode : {0xC0, 0xC8, 0xD0, 0xD8}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                if (!conditionHolds(snapshot, GB::Decode::decodeCondition(code))) return;
                updateControlFlowPc(snapshot, pop16(snapshot), 1);
            });
        }));
    }

    setOpcode(0xC9, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            updateControlFlowPc(snapshot, pop16(snapshot), 1);
        });
    }));

    setOpcode(0xD9, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            updateControlFlowPc(snapshot, pop16(snapshot), 1);
            getRegister(snapshot, "ime")->value = 1;
        });
    }));

    for (DataType opcode : {0xC1, 0xD1, 0xE1, 0xF1}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                const auto target = GB::Decode::decodeR16Stack(code);
                AddressType value = pop16(snapshot);
                if (target == GB::Decode::R16Stack::AF) {
                    value = static_cast<AddressType>(value & 0xFFF0u);
                }
                accessStackR16(snapshot, target) = value;
            });
        }));
    }

    for (DataType opcode : {0xC5, 0xD5, 0xE5, 0xF5}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            block.addStep([code](auto& snapshot, auto&) {
                push16(snapshot, accessStackR16(snapshot, GB::Decode::decodeR16Stack(code)));
            });
        }));
    }

    for (DataType opcode : {0xC2, 0xCA, 0xD2, 0xDA}) {
        setOpcode(opcode, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType code) {
            const AddressType target = fetchImm16(fetchData, opcodeIndex);
            block.addStep([code, target](auto& snapshot, auto&) {
                if (!conditionHolds(snapshot, GB::Decode::decodeCondition(code))) return;
                updateControlFlowPc(snapshot, target, 3);
            });
        }));
    }

    setOpcode(0xC3, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType target = fetchImm16(fetchData, opcodeIndex);
        block.addStep([target](auto& snapshot, auto&) {
            updateControlFlowPc(snapshot, target, 3);
        });
    }));

    setOpcode(0xE9, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            updateControlFlowPc(snapshot, getRegisterPair(snapshot, GB::RegisterId::HL)->value, 1);
        });
    }));

    for (DataType opcode : {0xC4, 0xCC, 0xD4, 0xDC}) {
        setOpcode(opcode, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType code) {
            const AddressType target = fetchImm16(fetchData, opcodeIndex);
            block.addStep([code, target](auto& snapshot, auto&) {
                if (!conditionHolds(snapshot, GB::Decode::decodeCondition(code))) return;
                const AddressType pc = static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::PC)->value);
                push16(snapshot, static_cast<AddressType>(pc + 3));
                updateControlFlowPc(snapshot, target, 3);
            });
        }));
    }

    setOpcode(0xCD, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType target = fetchImm16(fetchData, opcodeIndex);
        block.addStep([target](auto& snapshot, auto&) {
            const AddressType pc = static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::PC)->value);
            push16(snapshot, static_cast<AddressType>(pc + 3));
            updateControlFlowPc(snapshot, target, 3);
        });
    }));

    for (DataType opcode : {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF}) {
        setOpcode(opcode, emitStep(1, [](auto& block, const auto&, std::size_t, DataType code) {
            const AddressType target = static_cast<AddressType>(code & 0x38u);
            block.addStep([target](auto& snapshot, auto&) {
                const AddressType pc = static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::PC)->value);
                push16(snapshot, static_cast<AddressType>(pc + 1));
                updateControlFlowPc(snapshot, target, 1);
            });
        }));
    }

    setOpcode(0xCB, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const DataType cbOpcode = fetchData.data[opcodeIndex + 1];
        block.addStep([cbOpcode](auto& snapshot, auto&) {
            executeCbOpcode(snapshot, cbOpcode);
        });
    }));

    setOpcode(0xE0, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType address = static_cast<AddressType>(0xFF00u + fetchImm8(fetchData, opcodeIndex));
        block.addStep([address](auto& snapshot, auto&) {
            write8(snapshot, address, accumulator(snapshot));
        });
    }));

    setOpcode(0xF0, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType address = static_cast<AddressType>(0xFF00u + fetchImm8(fetchData, opcodeIndex));
        block.addStep([address](auto& snapshot, auto&) {
            accumulator(snapshot) = read8(snapshot, address);
        });
    }));

    setOpcode(0xE2, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            const AddressType address = static_cast<AddressType>(0xFF00u + getRegisterPair(snapshot, GB::RegisterId::BC)->lo);
            write8(snapshot, address, accumulator(snapshot));
        });
    }));

    setOpcode(0xF2, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            const AddressType address = static_cast<AddressType>(0xFF00u + getRegisterPair(snapshot, GB::RegisterId::BC)->lo);
            accumulator(snapshot) = read8(snapshot, address);
        });
    }));

    setOpcode(0xE8, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const int8_t offset = static_cast<int8_t>(fetchImm8(fetchData, opcodeIndex));
        block.addStep([offset](auto& snapshot, auto&) {
            auto* sp = getRegister(snapshot, GB::RegisterId::SP);
            const AddressType original = sp->value;
            const AddressType result = static_cast<AddressType>(static_cast<int32_t>(original) + offset);
            const uint8_t low = static_cast<uint8_t>(original & 0xFFu);
            const uint8_t imm = static_cast<uint8_t>(offset);
            const bool h = ((low & 0x0Fu) + (imm & 0x0Fu)) > 0x0Fu;
            const bool c = static_cast<uint16_t>(low) + static_cast<uint16_t>(imm) > 0xFFu;
            setFlags(snapshot, false, false, h, c);
            sp->value = result;
        });
    }));

    setOpcode(0xF8, emitStep(2, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const int8_t offset = static_cast<int8_t>(fetchImm8(fetchData, opcodeIndex));
        block.addStep([offset](auto& snapshot, auto&) {
            const AddressType sp = static_cast<AddressType>(getRegister(snapshot, GB::RegisterId::SP)->value);
            const AddressType result = static_cast<AddressType>(static_cast<int32_t>(sp) + offset);
            const uint8_t low = static_cast<uint8_t>(sp & 0xFFu);
            const uint8_t imm = static_cast<uint8_t>(offset);
            const bool h = ((low & 0x0Fu) + (imm & 0x0Fu)) > 0x0Fu;
            const bool c = static_cast<uint16_t>(low) + static_cast<uint16_t>(imm) > 0xFFu;
            setFlags(snapshot, false, false, h, c);
            getRegisterPair(snapshot, GB::RegisterId::HL)->value = result;
        });
    }));

    setOpcode(0xF9, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            getRegister(snapshot, GB::RegisterId::SP)->value =
                getRegisterPair(snapshot, GB::RegisterId::HL)->value;
        });
    }));

    setOpcode(0xEA, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType address = fetchImm16(fetchData, opcodeIndex);
        block.addStep([address](auto& snapshot, auto&) {
            write8(snapshot, address, accumulator(snapshot));
        });
    }));

    setOpcode(0xFA, emitStep(3, [](auto& block, const auto& fetchData, std::size_t opcodeIndex, DataType) {
        const AddressType address = fetchImm16(fetchData, opcodeIndex);
        block.addStep([address](auto& snapshot, auto&) {
            accumulator(snapshot) = read8(snapshot, address);
        });
    }));

    setOpcode(0xF3, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            getRegister(snapshot, "ime")->value = 0;
        });
    }));

    setOpcode(0xFB, emitStep(1, [](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([](auto& snapshot, auto&) {
            getRegister(snapshot, "ime")->value = 1;
        });
    }));

}
