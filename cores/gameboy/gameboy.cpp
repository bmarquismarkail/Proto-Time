#include "gameboy.hpp"

#include "decode/gb_opcode_decode.hpp"
#include "hardware_registers.hpp"

#include <algorithm>
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

constexpr DataType kInterruptVBlank = 0x01;
constexpr DataType kInterruptLcdStat = 0x02;
constexpr DataType kInterruptTimer = 0x04;
constexpr DataType kInterruptSerial = 0x08;
constexpr DataType kInterruptJoypad = 0x10;
constexpr DataType kInterruptMask = 0x1F;

uint16_t timerBitMaskForTac(DataType tac)
{
    switch (tac & 0x03u) {
    case 0x00:
        return 1u << 9;
    case 0x01:
        return 1u << 3;
    case 0x02:
        return 1u << 5;
    case 0x03:
        return 1u << 7;
    }

    return 1u << 9;
}

bool isHramAddress(AddressType address)
{
    return address >= 0xFF80u && address <= 0xFFFEu;
}

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

std::size_t instructionCyclesFor(const BMMQ::fetchBlock<AddressType, DataType>& fetchBlock,
                                 AddressType pcBefore,
                                 AddressType pcAfter)
{
    const auto& blockData = fetchBlock.getblockData();
    if (blockData.empty() || blockData.front().data.empty()) {
        return 0;
    }

    const auto& data = blockData.front().data;
    const DataType opcode = data.front();
    const auto sequentialPc = static_cast<AddressType>(pcBefore + data.size());
    const bool tookBranch = pcAfter != sequentialPc;

    if (opcode >= 0x40u && opcode <= 0x7Fu && opcode != 0x76u) {
        const auto src = GB::Decode::decodeR8Src(opcode);
        const auto dest = GB::Decode::decodeR8Dest(opcode);
        return (src == GB::Decode::R8::HLIndirect || dest == GB::Decode::R8::HLIndirect) ? 8u : 4u;
    }

    if (opcode >= 0x80u && opcode <= 0xBFu) {
        return GB::Decode::decodeR8Src(opcode) == GB::Decode::R8::HLIndirect ? 8u : 4u;
    }

    switch (opcode) {
    case 0x00:
    case 0x07:
    case 0x0F:
    case 0x17:
    case 0x1F:
    case 0x27:
    case 0x2F:
    case 0x37:
    case 0x3F:
    case 0x76:
    case 0xE9:
    case 0xF3:
    case 0xFB:
        return 4u;

    case 0x01:
    case 0x11:
    case 0x21:
    case 0x31:
    case 0xC2:
    case 0xCA:
    case 0xD2:
    case 0xDA:
        return tookBranch ? 16u : 12u;

    case 0x03:
    case 0x13:
    case 0x23:
    case 0x33:
    case 0x0B:
    case 0x1B:
    case 0x2B:
    case 0x3B:
    case 0x09:
    case 0x19:
    case 0x29:
    case 0x39:
    case 0xE2:
    case 0xF2:
    case 0xF9:
        return 8u;

    case 0x04:
    case 0x0C:
    case 0x14:
    case 0x1C:
    case 0x24:
    case 0x2C:
    case 0x34:
    case 0x3C:
    case 0x05:
    case 0x0D:
    case 0x15:
    case 0x1D:
    case 0x25:
    case 0x2D:
    case 0x35:
    case 0x3D:
        return GB::Decode::decodeR8Dest(opcode) == GB::Decode::R8::HLIndirect ? 12u : 4u;

    case 0x06:
    case 0x0E:
    case 0x16:
    case 0x1E:
    case 0x26:
    case 0x2E:
    case 0x36:
    case 0x3E:
        return GB::Decode::decodeR8Dest(opcode) == GB::Decode::R8::HLIndirect ? 12u : 8u;

    case 0x08:
        return 20u;

    case 0x02:
    case 0x12:
    case 0x22:
    case 0x32:
    case 0x0A:
    case 0x1A:
    case 0x2A:
    case 0x3A:
    case 0x18:
    case 0xC6:
    case 0xCE:
    case 0xD6:
    case 0xDE:
    case 0xE6:
    case 0xEE:
    case 0xF6:
    case 0xFE:
        return 8u;

    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38:
        return tookBranch ? 12u : 8u;

    case 0xC0:
    case 0xC8:
    case 0xD0:
    case 0xD8:
        return tookBranch ? 20u : 8u;

    case 0xC1:
    case 0xD1:
    case 0xE1:
    case 0xF1:
        return 12u;

    case 0xC3:
    case 0xEA:
    case 0xFA:
        return 16u;

    case 0xC4:
    case 0xCC:
    case 0xD4:
    case 0xDC:
        return tookBranch ? 24u : 12u;

    case 0xC5:
    case 0xD5:
    case 0xE5:
    case 0xF5:
        return 16u;

    case 0xC7:
    case 0xCF:
    case 0xD7:
    case 0xDF:
    case 0xE7:
    case 0xEF:
    case 0xF7:
    case 0xFF:
        return 16u;

    case 0xC9:
    case 0xD9:
    case 0xE8:
        return 16u;

    case 0xCD:
        return 24u;

    case 0xCB:
        if (data.size() > 1 && GB::Decode::decodeR8(data[1]) == GB::Decode::R8::HLIndirect) {
            return 16u;
        }
        return 8u;

    case 0xE0:
    case 0xF0:
    case 0xF8:
        return 12u;
    }

    return static_cast<std::size_t>(data.size()) * 4u;
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
    initializeRegisterCache();
    loadProgram({0x3E, 0x12, 0x00});
    populateOpcodes();
    resetDivider();
    writeIoRegister("JOYP", 0xFF);
    initializeApu();
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
    store.setAddressTranslator([](AddressType address) {
        return LR3592_DMG::normalizeAccessAddress(address);
    });
    store.setReadInterceptor([this](AddressType address, std::span<DataType> value) {
        return handleMemoryRead(address, value);
    });
    store.setWriteInterceptor([this](AddressType address, std::span<const DataType> value) {
        return handleMemoryWrite(address, value);
    });
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

void LR3592_DMG::cacheRegisterRef(CachedRegisterRef& slot, std::string_view name, AddressType address)
{
    slot.address = address;
    if (auto* entry = mem.file.findRegister(name); entry != nullptr && entry->reg != nullptr) {
        slot.reg = entry->reg.get();
    }
}

void LR3592_DMG::initializeRegisterCache()
{
    if (auto* entry = mem.file.findRegister(GB::RegisterId::PC); entry != nullptr && entry->reg != nullptr) {
        pcRegister_ = entry->reg.get();
    }
    if (auto* entry = mem.file.findRegister(GB::RegisterId::SP); entry != nullptr && entry->reg != nullptr) {
        spRegister_ = entry->reg.get();
    }

    cacheRegisterRef(hardwareRegisters_.joyp, "JOYP", 0xFF00u);
    cacheRegisterRef(hardwareRegisters_.sb, "SB", 0xFF01u);
    cacheRegisterRef(hardwareRegisters_.sc, "SC", 0xFF02u);
    cacheRegisterRef(hardwareRegisters_.div, "DIV", 0xFF04u);
    cacheRegisterRef(hardwareRegisters_.tima, "TIMA", 0xFF05u);
    cacheRegisterRef(hardwareRegisters_.tma, "TMA", 0xFF06u);
    cacheRegisterRef(hardwareRegisters_.tac, "TAC", 0xFF07u);
    cacheRegisterRef(hardwareRegisters_.interruptFlags, "IF", 0xFF0Fu);
    cacheRegisterRef(hardwareRegisters_.lcdc, "LCDC", 0xFF40u);
    cacheRegisterRef(hardwareRegisters_.stat, "STAT", 0xFF41u);
    cacheRegisterRef(hardwareRegisters_.ly, "LY", 0xFF44u);
    cacheRegisterRef(hardwareRegisters_.lyc, "LYC", 0xFF45u);
    cacheRegisterRef(hardwareRegisters_.dma, "DMA", 0xFF46u);
    cacheRegisterRef(hardwareRegisters_.ie, GB::RegisterId::IE, 0xFFFFu);
}

DataType LR3592_DMG::readCachedRegister(const CachedRegisterRef& slot)
{
    if (slot.reg == nullptr) {
        return 0;
    }
    return static_cast<DataType>(slot.reg->value & 0x00FFu);
}

const LR3592_DMG::CachedRegisterRef* LR3592_DMG::cachedIoRegisterForAddress(AddressType address) const
{
    switch (address) {
    case 0xFF01u:
        return &hardwareRegisters_.sb;
    case 0xFF02u:
        return &hardwareRegisters_.sc;
    case 0xFF04u:
        return &hardwareRegisters_.div;
    case 0xFF05u:
        return &hardwareRegisters_.tima;
    case 0xFF06u:
        return &hardwareRegisters_.tma;
    case 0xFF07u:
        return &hardwareRegisters_.tac;
    case 0xFF0Fu:
        return &hardwareRegisters_.interruptFlags;
    case 0xFF40u:
        return &hardwareRegisters_.lcdc;
    case 0xFF41u:
        return &hardwareRegisters_.stat;
    case 0xFF44u:
        return &hardwareRegisters_.ly;
    case 0xFF45u:
        return &hardwareRegisters_.lyc;
    case 0xFF46u:
        return &hardwareRegisters_.dma;
    case 0xFFFFu:
        return &hardwareRegisters_.ie;
    default:
        return nullptr;
    }
}

void LR3592_DMG::writeCachedRegister(const CachedRegisterRef& slot, DataType value)
{
    if (slot.reg == nullptr) {
        return;
    }

    if (static_cast<DataType>(slot.reg->value & 0x00FFu) == value) {
        return;
    }

    slot.reg->value = value;
    mem.backingStore().load(std::span<const DataType>(&value, 1), slot.address);
}

DataType LR3592_DMG::readIoRegister(std::string_view name) const
{
    if (name == "JOYP") return readCachedRegister(hardwareRegisters_.joyp);
    if (name == "SB") return readCachedRegister(hardwareRegisters_.sb);
    if (name == "SC") return readCachedRegister(hardwareRegisters_.sc);
    if (name == "DIV") return readCachedRegister(hardwareRegisters_.div);
    if (name == "TIMA") return readCachedRegister(hardwareRegisters_.tima);
    if (name == "TMA") return readCachedRegister(hardwareRegisters_.tma);
    if (name == "TAC") return readCachedRegister(hardwareRegisters_.tac);
    if (name == "IF") return readCachedRegister(hardwareRegisters_.interruptFlags);
    if (name == "LCDC") return readCachedRegister(hardwareRegisters_.lcdc);
    if (name == "STAT") return readCachedRegister(hardwareRegisters_.stat);
    if (name == "LY") return readCachedRegister(hardwareRegisters_.ly);
    if (name == "LYC") return readCachedRegister(hardwareRegisters_.lyc);
    if (name == "DMA") return readCachedRegister(hardwareRegisters_.dma);
    if (name == GB::RegisterId::IE) return readCachedRegister(hardwareRegisters_.ie);

    auto* entry = mem.file.findRegister(name);
    if (entry == nullptr || entry->reg == nullptr) {
        return 0;
    }
    return static_cast<DataType>(entry->reg->value & 0x00FFu);
}

void LR3592_DMG::writeIoRegister(std::string_view name, DataType value)
{
    if (name == "JOYP") { writeCachedRegister(hardwareRegisters_.joyp, value); return; }
    if (name == "SB") { writeCachedRegister(hardwareRegisters_.sb, value); return; }
    if (name == "SC") { writeCachedRegister(hardwareRegisters_.sc, value); return; }
    if (name == "DIV") { writeCachedRegister(hardwareRegisters_.div, value); return; }
    if (name == "TIMA") { writeCachedRegister(hardwareRegisters_.tima, value); return; }
    if (name == "TMA") { writeCachedRegister(hardwareRegisters_.tma, value); return; }
    if (name == "TAC") { writeCachedRegister(hardwareRegisters_.tac, value); return; }
    if (name == "IF") { writeCachedRegister(hardwareRegisters_.interruptFlags, value); return; }
    if (name == "LCDC") { writeCachedRegister(hardwareRegisters_.lcdc, value); return; }
    if (name == "STAT") { writeCachedRegister(hardwareRegisters_.stat, value); return; }
    if (name == "LY") { writeCachedRegister(hardwareRegisters_.ly, value); return; }
    if (name == "LYC") { writeCachedRegister(hardwareRegisters_.lyc, value); return; }
    if (name == "DMA") { writeCachedRegister(hardwareRegisters_.dma, value); return; }
    if (name == GB::RegisterId::IE) { writeCachedRegister(hardwareRegisters_.ie, value); return; }

    auto* entry = mem.file.findRegister(name);
    if (entry == nullptr || entry->reg == nullptr) {
        return;
    }
    entry->reg->value = value;

    const auto* descriptor = mem.file.findDescriptor(name);
    if (descriptor != nullptr && descriptor->mappedAddress.has_value()) {
        mem.backingStore().load(std::span<const DataType>(&value, 1), *descriptor->mappedAddress);
    }
}

void LR3592_DMG::initializeApu()
{
    apu_ = {};
    apu_.masterEnabled = (readIoRegister("NR52") & 0x80u) != 0u;
    apu_.pulse1.hasSweep = true;
    updateApuStatusRegister();
}

std::vector<int16_t> LR3592_DMG::copyRecentAudioSamples() const
{
    std::vector<int16_t> samples;
    samples.reserve(apu_.recentSampleCount);
    if (apu_.recentSampleCount == 0u) {
        return samples;
    }

    const auto start = (apu_.recentWriteCursor + kApuHistorySamples - apu_.recentSampleCount) % kApuHistorySamples;
    for (std::size_t i = 0; i < apu_.recentSampleCount; ++i) {
        samples.push_back(apu_.recentSamples[(start + i) % kApuHistorySamples]);
    }
    return samples;
}

uint16_t LR3592_DMG::pulseTimerPeriod(uint16_t frequency) const
{
    const auto sanitized = static_cast<uint16_t>(frequency & 0x07FFu);
    return static_cast<uint16_t>(std::max<uint32_t>(4u, (2048u - sanitized) * 4u));
}

uint16_t LR3592_DMG::waveTimerPeriod(uint16_t frequency) const
{
    const auto sanitized = static_cast<uint16_t>(frequency & 0x07FFu);
    return static_cast<uint16_t>(std::max<uint32_t>(2u, (2048u - sanitized) * 2u));
}

uint16_t LR3592_DMG::noiseTimerPeriod() const
{
    static constexpr std::array<uint16_t, 8> kDivisors{{8u, 16u, 32u, 48u, 64u, 80u, 96u, 112u}};
    const auto divisor = kDivisors[apu_.noise.divisorCode & 0x07u];
    const auto period = static_cast<uint32_t>(divisor) << apu_.noise.clockShift;
    return static_cast<uint16_t>(std::min<uint32_t>(std::max<uint32_t>(period, 8u), 0xFFFFu));
}

namespace {

template <typename Channel>
void tickApuEnvelopeImpl(Channel& channel)
{
    if (!channel.enabled || channel.envelopePeriod == 0u) {
        return;
    }
    if (channel.envelopeTimer > 0u) {
        --channel.envelopeTimer;
    }
    if (channel.envelopeTimer != 0u) {
        return;
    }
    channel.envelopeTimer = channel.envelopePeriod;
    if (channel.envelopeIncrease) {
        if (channel.volume < 15u) {
            ++channel.volume;
        }
    } else if (channel.volume > 0u) {
        --channel.volume;
    }
}

} // namespace

void LR3592_DMG::tickApuEnvelope(ApuPulseChannel& channel)
{
    tickApuEnvelopeImpl(channel);
}

void LR3592_DMG::tickApuEnvelope(ApuNoiseChannel& channel)
{
    tickApuEnvelopeImpl(channel);
}

void LR3592_DMG::tickApuLengthCounters()
{
    bool changed = false;
    const auto tickPulseLength = [&changed](ApuPulseChannel& channel) {
        if (channel.enabled && channel.lengthEnabled && channel.lengthCounter > 0u) {
            --channel.lengthCounter;
            if (channel.lengthCounter == 0u) {
                channel.enabled = false;
                changed = true;
            }
        }
    };

    tickPulseLength(apu_.pulse1);
    tickPulseLength(apu_.pulse2);
    if (apu_.wave.enabled && apu_.wave.lengthEnabled && apu_.wave.lengthCounter > 0u) {
        --apu_.wave.lengthCounter;
        if (apu_.wave.lengthCounter == 0u) {
            apu_.wave.enabled = false;
            changed = true;
        }
    }
    if (apu_.noise.enabled && apu_.noise.lengthEnabled && apu_.noise.lengthCounter > 0u) {
        --apu_.noise.lengthCounter;
        if (apu_.noise.lengthCounter == 0u) {
            apu_.noise.enabled = false;
            changed = true;
        }
    }

    if (changed) {
        updateApuStatusRegister();
    }
}

void LR3592_DMG::tickApuSweep()
{
    auto& channel = apu_.pulse1;
    if (!channel.hasSweep || !channel.enabled || !channel.sweepEnabled) {
        return;
    }
    if (channel.sweepTimer > 0u) {
        --channel.sweepTimer;
    }
    if (channel.sweepTimer != 0u) {
        return;
    }

    channel.sweepTimer = channel.sweepPeriod == 0u ? 8u : channel.sweepPeriod;
    if (channel.sweepPeriod == 0u) {
        return;
    }

    const uint16_t delta = static_cast<uint16_t>(channel.shadowFrequency >> channel.sweepShift);
    const int nextFrequency = channel.sweepNegate
        ? static_cast<int>(channel.shadowFrequency) - static_cast<int>(delta)
        : static_cast<int>(channel.shadowFrequency) + static_cast<int>(delta);
    if (nextFrequency < 0 || nextFrequency > 2047) {
        channel.enabled = false;
        updateApuStatusRegister();
        return;
    }

    if (channel.sweepShift != 0u) {
        channel.shadowFrequency = static_cast<uint16_t>(nextFrequency);
        channel.frequency = channel.shadowFrequency;
        channel.timer = pulseTimerPeriod(channel.frequency);
        if (!channel.sweepNegate) {
            const uint16_t overflowCheck = static_cast<uint16_t>(channel.shadowFrequency + (channel.shadowFrequency >> channel.sweepShift));
            if (overflowCheck > 2047u) {
                channel.enabled = false;
            }
        }
        updateApuStatusRegister();
    }
}

void LR3592_DMG::stepApuFrameSequencer()
{
    const auto step = apu_.frameSequencerStep;
    if ((step & 0x01u) == 0u) {
        tickApuLengthCounters();
    }
    if (step == 2u || step == 6u) {
        tickApuSweep();
    }
    if (step == 7u) {
        tickApuEnvelope(apu_.pulse1);
        tickApuEnvelope(apu_.pulse2);
        tickApuEnvelope(apu_.noise);
    }
    apu_.frameSequencerStep = static_cast<uint8_t>((apu_.frameSequencerStep + 1u) & 0x07u);
}

void LR3592_DMG::triggerApuPulse(ApuPulseChannel& channel, DataType control, DataType envelope, bool withSweep)
{
    channel.duty = static_cast<uint8_t>((control >> 6) & 0x03u);
    if (channel.lengthCounter == 0u) {
        channel.lengthCounter = 64u;
    }
    channel.initialVolume = static_cast<uint8_t>((envelope >> 4) & 0x0Fu);
    channel.volume = channel.initialVolume;
    channel.envelopeIncrease = (envelope & 0x08u) != 0u;
    channel.envelopePeriod = static_cast<uint8_t>(envelope & 0x07u);
    channel.envelopeTimer = channel.envelopePeriod == 0u ? 8u : channel.envelopePeriod;
    channel.dacEnabled = (envelope & 0xF8u) != 0u;
    channel.enabled = apu_.masterEnabled && channel.dacEnabled;
    channel.timer = pulseTimerPeriod(channel.frequency);
    channel.dutyStep = 0u;

    if (withSweep) {
        const auto sweep = readIoRegister("NR10");
        channel.sweepPeriod = static_cast<uint8_t>((sweep >> 4) & 0x07u);
        channel.sweepNegate = (sweep & 0x08u) != 0u;
        channel.sweepShift = static_cast<uint8_t>(sweep & 0x07u);
        channel.shadowFrequency = channel.frequency;
        channel.sweepTimer = channel.sweepPeriod == 0u ? 8u : channel.sweepPeriod;
        channel.sweepEnabled = channel.sweepPeriod != 0u || channel.sweepShift != 0u;
        if (channel.sweepShift != 0u) {
            const uint16_t delta = static_cast<uint16_t>(channel.shadowFrequency >> channel.sweepShift);
            const int previewFrequency = channel.sweepNegate
                ? static_cast<int>(channel.shadowFrequency) - static_cast<int>(delta)
                : static_cast<int>(channel.shadowFrequency) + static_cast<int>(delta);
            if (previewFrequency < 0 || previewFrequency > 2047) {
                channel.enabled = false;
            }
        }
    }

    updateApuStatusRegister();
}

void LR3592_DMG::triggerApuWave()
{
    auto& channel = apu_.wave;
    if (channel.lengthCounter == 0u) {
        channel.lengthCounter = 256u;
    }
    channel.dacEnabled = (readIoRegister("NR30") & 0x80u) != 0u;
    channel.enabled = apu_.masterEnabled && channel.dacEnabled;
    channel.timer = waveTimerPeriod(channel.frequency);
    channel.sampleIndex = 0u;
    updateApuStatusRegister();
}

void LR3592_DMG::triggerApuNoise()
{
    auto& channel = apu_.noise;
    if (channel.lengthCounter == 0u) {
        channel.lengthCounter = 64u;
    }

    const uint8_t nr42 = readIoRegister("NR42");
    const uint8_t nr43 = readIoRegister("NR43");
    channel.initialVolume = static_cast<uint8_t>((nr42 >> 4) & 0x0Fu);
    channel.volume = channel.initialVolume;
    channel.envelopeIncrease = (nr42 & 0x08u) != 0u;
    channel.envelopePeriod = static_cast<uint8_t>(nr42 & 0x07u);
    channel.envelopeTimer = channel.envelopePeriod == 0u ? 8u : channel.envelopePeriod;
    channel.clockShift = static_cast<uint8_t>((nr43 >> 4) & 0x0Fu);
    channel.widthMode7 = (nr43 & 0x08u) != 0u;
    channel.divisorCode = static_cast<uint8_t>(nr43 & 0x07u);
    channel.dacEnabled = (nr42 & 0xF8u) != 0u;
    channel.enabled = apu_.masterEnabled && channel.dacEnabled;
    channel.lfsr = 0x7FFFu;
    channel.timer = noiseTimerPeriod();
    updateApuStatusRegister();
}

int LR3592_DMG::currentPulseSample(const ApuPulseChannel& channel) const
{
    if (!apu_.masterEnabled || !channel.enabled || !channel.dacEnabled || channel.volume == 0u) {
        return 0;
    }

    static constexpr std::array<std::array<uint8_t, 8>, 4> kDutyPatterns{{
        {{0, 0, 0, 0, 0, 0, 0, 1}},
        {{1, 0, 0, 0, 0, 0, 0, 1}},
        {{1, 0, 0, 0, 0, 1, 1, 1}},
        {{0, 1, 1, 1, 1, 1, 1, 0}},
    }};
    const auto polarity = kDutyPatterns[channel.duty & 0x03u][channel.dutyStep & 0x07u] != 0u ? 1 : -1;
    return polarity * static_cast<int>(channel.volume);
}

int LR3592_DMG::currentWaveSample() const
{
    if (!apu_.masterEnabled || !apu_.wave.enabled || !apu_.wave.dacEnabled) {
        return 0;
    }

    const auto outputLevel = static_cast<uint8_t>((readIoRegister("NR32") >> 5) & 0x03u);
    if (outputLevel == 0u) {
        return 0;
    }

    const auto sampleIndex = static_cast<uint8_t>(apu_.wave.sampleIndex & 0x1Fu);
    const auto address = static_cast<AddressType>(0xFF30u + (sampleIndex / 2u));
    const auto packed = mem.backingStore().readableSpan(address, 1)[0];
    uint8_t sample = (sampleIndex & 0x01u) == 0u ? static_cast<uint8_t>((packed >> 4) & 0x0Fu)
                                                 : static_cast<uint8_t>(packed & 0x0Fu);
    if (outputLevel == 2u) {
        sample >>= 1u;
    } else if (outputLevel == 3u) {
        sample >>= 2u;
    }
    return (static_cast<int>(sample) - 8) * 2;
}

int LR3592_DMG::currentNoiseSample() const
{
    if (!apu_.masterEnabled || !apu_.noise.enabled || !apu_.noise.dacEnabled || apu_.noise.volume == 0u) {
        return 0;
    }
    const auto polarity = (apu_.noise.lfsr & 0x01u) == 0u ? 1 : -1;
    return polarity * static_cast<int>(apu_.noise.volume);
}

void LR3592_DMG::pushApuSample()
{
    const int ch1 = currentPulseSample(apu_.pulse1);
    const int ch2 = currentPulseSample(apu_.pulse2);
    const int ch3 = currentWaveSample();
    const int ch4 = currentNoiseSample();

    const auto nr50 = readIoRegister("NR50");
    const auto nr51 = readIoRegister("NR51");
    const int leftVolume = static_cast<int>((nr50 >> 4) & 0x07u) + 1;
    const int rightVolume = static_cast<int>(nr50 & 0x07u) + 1;

    int left = 0;
    int right = 0;
    if ((nr51 & 0x10u) != 0u) left += ch1;
    if ((nr51 & 0x20u) != 0u) left += ch2;
    if ((nr51 & 0x40u) != 0u) left += ch3;
    if ((nr51 & 0x80u) != 0u) left += ch4;
    if ((nr51 & 0x01u) != 0u) right += ch1;
    if ((nr51 & 0x02u) != 0u) right += ch2;
    if ((nr51 & 0x04u) != 0u) right += ch3;
    if ((nr51 & 0x08u) != 0u) right += ch4;

    left *= leftVolume;
    right *= rightVolume;
    const int mixed = std::clamp((left + right) * 32, -32768, 32767);
    apu_.recentSamples[apu_.recentWriteCursor] = static_cast<int16_t>(mixed);
    apu_.recentWriteCursor = (apu_.recentWriteCursor + 1u) % kApuHistorySamples;
    apu_.recentSampleCount = std::min<std::size_t>(apu_.recentSampleCount + 1u, kApuHistorySamples);
    ++apu_.sampleCounter;
    if ((apu_.sampleCounter % kApuFrameChunkSamples) == 0u) {
        ++apu_.frameCounter;
    }
}

void LR3592_DMG::stepApuOneCycle()
{
    if (apu_.masterEnabled) {
        ++apu_.frameSequencerCounter;
        if (apu_.frameSequencerCounter >= 8192u) {
            apu_.frameSequencerCounter = 0u;
            stepApuFrameSequencer();
        }

        const auto tickPulseTimer = [this](ApuPulseChannel& channel) {
            if (channel.timer > 0u) {
                --channel.timer;
            }
            if (channel.timer == 0u) {
                channel.timer = pulseTimerPeriod(channel.frequency);
                channel.dutyStep = static_cast<uint8_t>((channel.dutyStep + 1u) & 0x07u);
            }
        };

        tickPulseTimer(apu_.pulse1);
        tickPulseTimer(apu_.pulse2);

        if (apu_.wave.timer > 0u) {
            --apu_.wave.timer;
        }
        if (apu_.wave.timer == 0u) {
            apu_.wave.timer = waveTimerPeriod(apu_.wave.frequency);
            apu_.wave.sampleIndex = static_cast<uint8_t>((apu_.wave.sampleIndex + 1u) & 0x1Fu);
        }

        if (apu_.noise.timer > 0u) {
            --apu_.noise.timer;
        }
        if (apu_.noise.timer == 0u) {
            apu_.noise.timer = noiseTimerPeriod();
            const auto xorBit = static_cast<uint16_t>((apu_.noise.lfsr ^ (apu_.noise.lfsr >> 1u)) & 0x01u);
            apu_.noise.lfsr = static_cast<uint16_t>((apu_.noise.lfsr >> 1u) | (xorBit << 14u));
            if (apu_.noise.widthMode7) {
                apu_.noise.lfsr = static_cast<uint16_t>((apu_.noise.lfsr & ~(1u << 6u)) | (xorBit << 6u));
            }
        }
    }

    apu_.sampleAccumulator += kApuSampleRate;
    while (apu_.sampleAccumulator >= kCpuClockHz) {
        apu_.sampleAccumulator -= kCpuClockHz;
        pushApuSample();
    }
}

void LR3592_DMG::updateApuStatusRegister()
{
    const auto channelBits = static_cast<DataType>((apu_.pulse1.enabled ? 0x01u : 0u)
                           | (apu_.pulse2.enabled ? 0x02u : 0u)
                           | (apu_.wave.enabled ? 0x04u : 0u)
                           | (apu_.noise.enabled ? 0x08u : 0u));
    const auto value = static_cast<DataType>((apu_.masterEnabled ? 0x80u : 0x00u) | 0x70u | channelBits);
    writeIoRegister("NR52", value);
}

void LR3592_DMG::handleApuRegisterWrite(AddressType address, DataType value)
{
    const auto storeAt = [this](AddressType regAddress, DataType regValue) {
        for (const auto& spec : GB::HardwareRegisters::kSpecs) {
            if (spec.address == regAddress) {
                writeIoRegister(spec.name, regValue);
                return;
            }
        }
        mem.backingStore().load(std::span<const DataType>(&regValue, 1), regAddress);
    };

    if (address >= 0xFF30u && address <= 0xFF3Fu) {
        storeAt(address, value);
        return;
    }

    switch (address) {
    case 0xFF10u:
        storeAt(address, value);
        apu_.pulse1.hasSweep = true;
        apu_.pulse1.sweepPeriod = static_cast<uint8_t>((value >> 4) & 0x07u);
        apu_.pulse1.sweepNegate = (value & 0x08u) != 0u;
        apu_.pulse1.sweepShift = static_cast<uint8_t>(value & 0x07u);
        break;
    case 0xFF11u:
        storeAt(address, value);
        apu_.pulse1.duty = static_cast<uint8_t>((value >> 6) & 0x03u);
        apu_.pulse1.lengthCounter = static_cast<uint8_t>(64u - (value & 0x3Fu));
        break;
    case 0xFF12u:
        storeAt(address, value);
        apu_.pulse1.initialVolume = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.pulse1.envelopeIncrease = (value & 0x08u) != 0u;
        apu_.pulse1.envelopePeriod = static_cast<uint8_t>(value & 0x07u);
        apu_.pulse1.dacEnabled = (value & 0xF8u) != 0u;
        if (!apu_.pulse1.dacEnabled) {
            apu_.pulse1.enabled = false;
        }
        break;
    case 0xFF13u:
        storeAt(address, value);
        apu_.pulse1.frequency = static_cast<uint16_t>((apu_.pulse1.frequency & 0x0700u) | value);
        break;
    case 0xFF14u:
        storeAt(address, value);
        apu_.pulse1.lengthEnabled = (value & 0x40u) != 0u;
        apu_.pulse1.frequency = static_cast<uint16_t>((apu_.pulse1.frequency & 0x00FFu) | ((value & 0x07u) << 8u));
        if ((value & 0x80u) != 0u) {
            triggerApuPulse(apu_.pulse1, readIoRegister("NR11"), readIoRegister("NR12"), true);
        }
        break;
    case 0xFF16u:
        storeAt(address, value);
        apu_.pulse2.duty = static_cast<uint8_t>((value >> 6) & 0x03u);
        apu_.pulse2.lengthCounter = static_cast<uint8_t>(64u - (value & 0x3Fu));
        break;
    case 0xFF17u:
        storeAt(address, value);
        apu_.pulse2.initialVolume = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.pulse2.envelopeIncrease = (value & 0x08u) != 0u;
        apu_.pulse2.envelopePeriod = static_cast<uint8_t>(value & 0x07u);
        apu_.pulse2.dacEnabled = (value & 0xF8u) != 0u;
        if (!apu_.pulse2.dacEnabled) {
            apu_.pulse2.enabled = false;
        }
        break;
    case 0xFF18u:
        storeAt(address, value);
        apu_.pulse2.frequency = static_cast<uint16_t>((apu_.pulse2.frequency & 0x0700u) | value);
        break;
    case 0xFF19u:
        storeAt(address, value);
        apu_.pulse2.lengthEnabled = (value & 0x40u) != 0u;
        apu_.pulse2.frequency = static_cast<uint16_t>((apu_.pulse2.frequency & 0x00FFu) | ((value & 0x07u) << 8u));
        if ((value & 0x80u) != 0u) {
            triggerApuPulse(apu_.pulse2, readIoRegister("NR21"), readIoRegister("NR22"), false);
        }
        break;
    case 0xFF1Au:
        storeAt(address, value);
        apu_.wave.dacEnabled = (value & 0x80u) != 0u;
        if (!apu_.wave.dacEnabled) {
            apu_.wave.enabled = false;
        }
        break;
    case 0xFF1Bu:
        storeAt(address, value);
        apu_.wave.lengthCounter = static_cast<uint16_t>(256u - value);
        break;
    case 0xFF1Cu:
        storeAt(address, value);
        break;
    case 0xFF1Du:
        storeAt(address, value);
        apu_.wave.frequency = static_cast<uint16_t>((apu_.wave.frequency & 0x0700u) | value);
        break;
    case 0xFF1Eu:
        storeAt(address, value);
        apu_.wave.lengthEnabled = (value & 0x40u) != 0u;
        apu_.wave.frequency = static_cast<uint16_t>((apu_.wave.frequency & 0x00FFu) | ((value & 0x07u) << 8u));
        if ((value & 0x80u) != 0u) {
            triggerApuWave();
        }
        break;
    case 0xFF20u:
        storeAt(address, value);
        apu_.noise.lengthCounter = static_cast<uint8_t>(64u - (value & 0x3Fu));
        break;
    case 0xFF21u:
        storeAt(address, value);
        apu_.noise.initialVolume = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.noise.envelopeIncrease = (value & 0x08u) != 0u;
        apu_.noise.envelopePeriod = static_cast<uint8_t>(value & 0x07u);
        apu_.noise.dacEnabled = (value & 0xF8u) != 0u;
        if (!apu_.noise.dacEnabled) {
            apu_.noise.enabled = false;
        }
        break;
    case 0xFF22u:
        storeAt(address, value);
        apu_.noise.clockShift = static_cast<uint8_t>((value >> 4) & 0x0Fu);
        apu_.noise.widthMode7 = (value & 0x08u) != 0u;
        apu_.noise.divisorCode = static_cast<uint8_t>(value & 0x07u);
        break;
    case 0xFF23u:
        storeAt(address, value);
        apu_.noise.lengthEnabled = (value & 0x40u) != 0u;
        if ((value & 0x80u) != 0u) {
            triggerApuNoise();
        }
        break;
    case 0xFF24u:
    case 0xFF25u:
        storeAt(address, value);
        break;
    case 0xFF26u: {
        if ((value & 0x80u) == 0u) {
            apu_ = {};
            apu_.pulse1.hasSweep = true;
            apu_.masterEnabled = false;
            for (AddressType reg = 0xFF10u; reg <= 0xFF25u; ++reg) {
                if (reg == 0xFF15u || reg == 0xFF1Fu) {
                    continue;
                }
                storeAt(reg, 0x00u);
            }
            updateApuStatusRegister();
            return;
        }
        if (!apu_.masterEnabled) {
            apu_.masterEnabled = true;
            apu_.frameSequencerCounter = 0u;
            apu_.frameSequencerStep = 0u;
        }
        break;
    }
    default:
        storeAt(address, value);
        break;
    }

    updateApuStatusRegister();
}

DataType LR3592_DMG::joypadLowNibble() const
{
    DataType low = 0x0Fu;
    if ((joypSelect & 0x10u) == 0) {
        low = static_cast<DataType>(low & ~(joypadPressedMask & 0x0Fu));
    }
    if ((joypSelect & 0x20u) == 0) {
        low = static_cast<DataType>(low & ~((joypadPressedMask >> 4) & 0x0Fu));
    }
    return static_cast<DataType>(low & 0x0Fu);
}

void LR3592_DMG::setJoypadState(DataType pressedMask)
{
    const DataType oldLow = joypadLowNibble();
    const DataType oldPressedMask = joypadPressedMask;
    joypadPressedMask = pressedMask;
    const DataType newLow = joypadLowNibble();
    writeIoRegister("JOYP", static_cast<DataType>(0xC0u | joypSelect | newLow));

    const DataType newlyPressed = static_cast<DataType>(pressedMask & static_cast<DataType>(~oldPressedMask));
    if (newlyPressed != 0) {
        stopFlag = false;
    }

    if ((oldLow & static_cast<DataType>(~newLow)) != 0) {
        requestInterrupt(kInterruptJoypad);
    }
}

void LR3592_DMG::requestInterrupt(DataType mask)
{
    const auto current = readCachedRegister(hardwareRegisters_.interruptFlags);
    writeCachedRegister(hardwareRegisters_.interruptFlags, static_cast<DataType>((current | mask) & kInterruptMask));
}

bool LR3592_DMG::serviceInterruptIfPending()
{
    const DataType enabled = readCachedRegister(hardwareRegisters_.ie);
    const DataType requested = readCachedRegister(hardwareRegisters_.interruptFlags);
    const DataType pending = static_cast<DataType>((enabled & requested) & kInterruptMask);
    if (pending == 0) {
        return false;
    }

    haltFlag = false;
    if (!ime) {
        return false;
    }

    DataType servicedMask = 0;
    AddressType vector = 0x0040;
    if ((pending & kInterruptVBlank) != 0) {
        servicedMask = kInterruptVBlank;
        vector = 0x0040;
    } else if ((pending & kInterruptLcdStat) != 0) {
        servicedMask = kInterruptLcdStat;
        vector = 0x0048;
    } else if ((pending & kInterruptTimer) != 0) {
        servicedMask = kInterruptTimer;
        vector = 0x0050;
    } else if ((pending & kInterruptSerial) != 0) {
        servicedMask = kInterruptSerial;
        vector = 0x0058;
    } else {
        servicedMask = kInterruptJoypad;
        vector = 0x0060;
    }

    writeCachedRegister(hardwareRegisters_.interruptFlags, static_cast<DataType>(requested & ~servicedMask));
    ime = false;
    imeEnablePending = false;
    imeEnableDelay = 0;

    if (pcRegister_ != nullptr && spRegister_ != nullptr) {
        const AddressType currentPc = static_cast<AddressType>(pcRegister_->value);
        spRegister_->value = static_cast<AddressType>(spRegister_->value - 2);
        std::array<DataType, 2> bytes {
            static_cast<DataType>(currentPc & 0x00FFu),
            static_cast<DataType>((currentPc >> 8) & 0x00FFu)
        };
        mem.write(std::span<const DataType>(bytes.data(), bytes.size()), static_cast<AddressType>(spRegister_->value));
        pcRegister_->value = vector;
    }

    pendingCycleCharge_ = 20u;
    return true;
}

void LR3592_DMG::retireInstruction(std::size_t executedCycles)
{
    const auto cycles = static_cast<uint32_t>(executedCycles);
    if (cycles == 0u) {
        return;
    }

    DataType interruptFlags = readCachedRegister(hardwareRegisters_.interruptFlags);
    DataType sb = readCachedRegister(hardwareRegisters_.sb);
    DataType sc = readCachedRegister(hardwareRegisters_.sc);
    DataType tima = readCachedRegister(hardwareRegisters_.tima);
    const DataType tma = readCachedRegister(hardwareRegisters_.tma);
    const DataType tac = readCachedRegister(hardwareRegisters_.tac);
    const bool timerEnabled = (tac & 0x04u) != 0;
    const uint16_t timerBitMask = timerBitMaskForTac(tac);

    const DataType lcdc = readCachedRegister(hardwareRegisters_.lcdc);
    DataType stat = readCachedRegister(hardwareRegisters_.stat);
    DataType ly = readCachedRegister(hardwareRegisters_.ly);
    const DataType lyc = readCachedRegister(hardwareRegisters_.lyc);
    const bool lcdEnabled = (lcdc & 0x80u) != 0;

    bool serialDirty = false;
    bool timerDirty = false;
    bool interruptDirty = false;
    bool statDirty = false;
    bool lyDirty = false;

    for (uint32_t i = 0; i < cycles; ++i) {
        if (dmaActive) {
            if ((dmaCycleProgress % 4u) == 0u) {
                const auto byteIndex = static_cast<std::size_t>(dmaCycleProgress / 4u);
                if (byteIndex < 0xA0u) {
                    AddressType sourceAddress = static_cast<AddressType>(dmaSourceBase + byteIndex);
                    sourceAddress = normalizeAccessAddress(sourceAddress);

                    DataType byte = 0xFF;
                    if (!(sourceAddress >= 0xFEA0u && sourceAddress <= 0xFEFFu)) {
                        byte = mem.backingStore().readableSpan(sourceAddress, 1)[0];
                    }
                    mem.backingStore().load(std::span<const DataType>(&byte, 1), static_cast<AddressType>(0xFE00u + byteIndex));
                }
            }

            ++dmaCycleProgress;
            if (dmaCycleProgress >= 0xA0u * 4u) {
                dmaActive = false;
                dmaCycleProgress = 0;
            }
        }

        if (serialTransferActive) {
            ++serialCycleProgress;
            if (serialCycleProgress >= 4096u) {
                serialTransferActive = false;
                serialCycleProgress = 0;
                sb = 0xFFu;
                sc = static_cast<DataType>(sc & 0x7Fu);
                serialDirty = true;
                interruptFlags = static_cast<DataType>((interruptFlags | kInterruptSerial) & kInterruptMask);
                interruptDirty = true;
            }
        }

        const bool oldSignal = timerEnabled && ((dividerCounter & timerBitMask) != 0);
        dividerCounter = static_cast<uint16_t>(dividerCounter + 1u);
        const bool newSignal = timerEnabled && ((dividerCounter & timerBitMask) != 0);
        if (oldSignal && !newSignal) {
            if (tima == 0xFFu) {
                tima = tma;
                interruptFlags = static_cast<DataType>((interruptFlags | kInterruptTimer) & kInterruptMask);
                interruptDirty = true;
            } else {
                tima = static_cast<DataType>(tima + 1u);
            }
            timerDirty = true;
        }

        stepApuOneCycle();

        if (!lcdEnabled) {
            ppuDotCounter = 0;
            lcdEnabledLastTick = false;
            statInterruptLatched = false;
            ly = 0;
            lyDirty = true;
            stat = static_cast<DataType>(stat & 0xF8u);
            if (lyc == 0) {
                stat = static_cast<DataType>(stat | 0x04u);
            } else {
                stat = static_cast<DataType>(stat & ~0x04u);
            }
            statDirty = true;
            continue;
        }

        if (!lcdEnabledLastTick) {
            ppuDotCounter = 0;
            ly = 0;
            lyDirty = true;
            lcdEnabledLastTick = true;
            statInterruptLatched = false;
        }

        const auto previousLy = static_cast<DataType>(ppuDotCounter / 456u);
        ppuDotCounter = (ppuDotCounter + 1u) % (154u * 456u);

        ly = static_cast<DataType>(ppuDotCounter / 456u);
        lyDirty = true;
        const auto lineDot = static_cast<uint16_t>(ppuDotCounter % 456u);

        DataType mode = 0;
        if (ly >= 144u) {
            mode = 1;
        } else if (lineDot < 80u) {
            mode = 2;
        } else if (lineDot < 252u) {
            mode = 3;
        } else {
            mode = 0;
        }

        stat = static_cast<DataType>((stat & 0xF8u) | mode);
        const bool coincidence = ly == lyc;
        if (coincidence) {
            stat = static_cast<DataType>(stat | 0x04u);
        } else {
            stat = static_cast<DataType>(stat & ~0x04u);
        }
        statDirty = true;

        const bool statInterruptRequested =
            (coincidence && (stat & 0x40u) != 0) ||
            (mode == 0 && (stat & 0x08u) != 0) ||
            (mode == 1 && (stat & 0x10u) != 0) ||
            (mode == 2 && (stat & 0x20u) != 0);
        if (statInterruptRequested && !statInterruptLatched) {
            interruptFlags = static_cast<DataType>((interruptFlags | kInterruptLcdStat) & kInterruptMask);
            interruptDirty = true;
        }
        statInterruptLatched = statInterruptRequested;

        if (previousLy != 144u && ly == 144u) {
            interruptFlags = static_cast<DataType>((interruptFlags | kInterruptVBlank) & kInterruptMask);
            interruptDirty = true;
        }
    }

    writeCachedRegister(hardwareRegisters_.div, static_cast<DataType>((dividerCounter >> 8) & 0x00FFu));
    if (serialDirty) {
        writeCachedRegister(hardwareRegisters_.sb, sb);
        writeCachedRegister(hardwareRegisters_.sc, sc);
    }
    if (timerDirty) {
        writeCachedRegister(hardwareRegisters_.tima, tima);
    }
    if (lyDirty) {
        writeCachedRegister(hardwareRegisters_.ly, ly);
    }
    if (statDirty) {
        writeCachedRegister(hardwareRegisters_.stat, stat);
    }
    if (interruptDirty) {
        writeCachedRegister(hardwareRegisters_.interruptFlags, interruptFlags);
    }

    if (imeEnablePending) {
        if (imeEnableDelay > 0) {
            --imeEnableDelay;
        } else {
            ime = true;
            imeEnablePending = false;
        }
    }
}

BMMQ::fetchBlock<AddressType, DataType> LR3592_DMG::fetch()
{
    BMMQ::fetchBlock<AddressType, DataType> f;
    f.reserve(1);
    fetchInto(f);
    return f;
};

void LR3592_DMG::fetchInto(BMMQ::fetchBlock<AddressType, DataType>& f)
{
    auto& blockData = f.getblockData();
    if (blockData.empty()) {
        blockData.emplace_back();
        blockData[0].data.reserve(3);
    } else if (blockData.size() > 1) {
        blockData.resize(1);
    }

    if (stopFlag) {
        f.setbaseAddress(0);
        blockData[0].offset = 0;
        blockData[0].data.clear();
        return;
    }

    const auto pc = pcRegister_ != nullptr
        ? static_cast<AddressType>(pcRegister_->value)
        : static_cast<AddressType>(0);

    if (dmaActive && !isHramAddress(pc)) {
        f.setbaseAddress(pc);
        blockData[0].offset = 0;
        blockData[0].data.clear();
        return;
    }

    const DataType pending = static_cast<DataType>((readCachedRegister(hardwareRegisters_.interruptFlags) & readCachedRegister(hardwareRegisters_.ie)) & kInterruptMask);
    if (haltFlag && pending == 0) {
        f.setbaseAddress(pc);
        blockData[0].offset = 0;
        blockData[0].data.clear();
        return;
    }

    const bool triggerHaltBug = haltFlag && pending != 0 && !ime;
    if (triggerHaltBug) {
        haltFlag = false;
        haltBugActive = true;
    }

    if (serviceInterruptIfPending()) {
        f.setbaseAddress(pcRegister_ != nullptr ? static_cast<AddressType>(pcRegister_->value) : pc);
        blockData[0].offset = 0;
        blockData[0].data.clear();
        return;
    }

    f.setbaseAddress(pc);

    auto& stream = blockData[0].data;
    blockData[0].offset = 0;
    stream.resize(1, 0);
    mem.read(std::span<DataType>(stream.data(), stream.size()), pc);

    const bool useHaltBugFetch = haltBugActive;
    haltBugActive = false;
    haltBugPcAdjustPending = useHaltBugFetch;

    const auto opcodeByte = stream[0];
    if (const auto& entry = opcodeTable[opcodeByte]; entry.has_value()) {
        const auto opcodeLength = entry->length();
        if (opcodeLength > stream.size()) {
            stream.resize(opcodeLength, 0);
            if (useHaltBugFetch) {
                for (std::size_t i = 1; i < opcodeLength; ++i) {
                    DataType byte = 0;
                    mem.read(std::span<DataType>(&byte, 1), static_cast<AddressType>(pc + i - 1u));
                    stream[i] = byte;
                }
            } else {
                mem.read(std::span<DataType>(stream.data(), stream.size()), pc);
            }
        }
    }
}

void LR3592_DMG::loadProgram(const std::vector<DataType>& program,
                             AddressType startAddress)
{
    mem.backingStore().load(std::span<const DataType>(program.data(), program.size()), startAddress);
}

BMMQ::executionBlock<AddressType, DataType, AddressType>
LR3592_DMG::decode(BMMQ::fetchBlock<AddressType, DataType>& fetchData)
{
    BMMQ::executionBlock<AddressType, DataType, AddressType> block;
    block.reserve(4);
    decodeInto(fetchData, block);
    return block;
}

void LR3592_DMG::decodeInto(BMMQ::fetchBlock<AddressType, DataType>& fetchData,
                            BMMQ::executionBlock<AddressType, DataType, AddressType>& block)
{
    block.clear();
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
}

bool LR3592_DMG::tryFastExecute(BMMQ::fetchBlock<AddressType, DataType>& fb)
{
    const auto& blocks = fb.getblockData();
    if (blocks.empty() || blocks.front().data.empty()) {
        return false;
    }

    auto& data = blocks.front().data;
    const DataType opcode = data[0];
    feedback.pcBefore = (pcRegister_ != nullptr)
        ? static_cast<uint32_t>(pcRegister_->value)
        : 0;
    feedback.isControlFlow = isControlFlowOpcode(opcode);
    feedback.segmentBoundaryHint = feedback.isControlFlow;

    auto finalizeFast = [&](std::size_t executedByteCount) {
        std::size_t pcAdvance = executedByteCount;
        if (haltBugPcAdjustPending && pcAdvance > 0) {
            --pcAdvance;
        }
        haltBugPcAdjustPending = false;

        if (pcRegister_ != nullptr) {
            pcRegister_->value = static_cast<AddressType>(pcRegister_->value + pcAdvance);
            feedback.pcAfter = static_cast<uint32_t>(pcRegister_->value);
        } else {
            feedback.pcAfter = feedback.pcBefore;
        }

        feedback.executionPath = BMMQ::ExecutionPathHint::CpuOptimizedFastPath;

        std::size_t retiredCycles = pendingCycleCharge_;
        pendingCycleCharge_ = 0;
        if (retiredCycles == 0) {
            retiredCycles = instructionCyclesFor(
                fb,
                static_cast<AddressType>(feedback.pcBefore),
                static_cast<AddressType>(feedback.pcAfter));
        }
        retireInstruction(retiredCycles);
        return true;
    };

    auto executeMathOp = [&](DataType code, DataType rhs) {
        auto& a = accumulator(mem);
        const DataType lhs = a;
        const bool carryIn = (flags(mem) & kFlagC) != 0;

        DataType result = lhs;
        bool z = false;
        bool n = false;
        bool h = false;
        bool c = false;

        switch ((code >> 3) & 0x07u) {
        case 0: {
            result = static_cast<DataType>(lhs + rhs);
            h = static_cast<uint16_t>((lhs & 0x0Fu) + (rhs & 0x0Fu)) > 0x0Fu;
            c = static_cast<uint16_t>(lhs) + static_cast<uint16_t>(rhs) > 0xFFu;
            break;
        }
        case 1: {
            const uint16_t sum = static_cast<uint16_t>(lhs) + static_cast<uint16_t>(rhs) + (carryIn ? 1u : 0u);
            result = static_cast<DataType>(sum);
            h = static_cast<uint16_t>((lhs & 0x0Fu) + (rhs & 0x0Fu) + (carryIn ? 1u : 0u)) > 0x0Fu;
            c = sum > 0xFFu;
            break;
        }
        case 2:
            result = static_cast<DataType>(lhs - rhs);
            h = (lhs & 0x0Fu) < (rhs & 0x0Fu);
            c = lhs < rhs;
            n = true;
            break;
        case 3: {
            const uint16_t subtrahend = static_cast<uint16_t>(rhs) + (carryIn ? 1u : 0u);
            result = static_cast<DataType>(lhs - subtrahend);
            h = (lhs & 0x0Fu) < ((rhs & 0x0Fu) + (carryIn ? 1u : 0u));
            c = static_cast<uint16_t>(lhs) < subtrahend;
            n = true;
            break;
        }
        case 4:
            result = static_cast<DataType>(lhs & rhs);
            h = true;
            break;
        case 5:
            result = static_cast<DataType>(lhs ^ rhs);
            break;
        case 6:
            result = static_cast<DataType>(lhs | rhs);
            break;
        case 7:
            h = (lhs & 0x0Fu) < (rhs & 0x0Fu);
            c = lhs < rhs;
            z = lhs == rhs;
            n = true;
            setFlags(mem, z, n, h, c);
            return;
        }

        a = result;
        z = (a == 0);
        setFlags(mem, z, n, h, c);
    };

    if (opcode >= 0x40u && opcode <= 0x7Fu && opcode != 0x76u) {
        writeR8(mem, GB::Decode::decodeR8Dest(opcode), readR8(mem, GB::Decode::decodeR8Src(opcode)));
        return finalizeFast(1);
    }

    if (opcode >= 0x80u && opcode <= 0xBFu) {
        executeMathOp(opcode, readR8(mem, GB::Decode::decodeR8Src(opcode)));
        return finalizeFast(1);
    }

    switch (opcode) {
    case 0x00:
        return finalizeFast(1);
    case 0x10:
        setStopFlag(true);
        return finalizeFast(2);
    case 0x76:
        setHaltFlag(true);
        return finalizeFast(1);
    case 0x01:
    case 0x11:
    case 0x21:
    case 0x31:
        accessR16(mem, GB::Decode::decodeR16(opcode)) = fetchImm16(blocks.front(), 0);
        return finalizeFast(3);
    case 0x03:
    case 0x13:
    case 0x23:
    case 0x33: {
        auto& reg = accessR16(mem, GB::Decode::decodeR16(opcode));
        reg = static_cast<AddressType>(reg + 1u);
        return finalizeFast(1);
    }
    case 0x0B:
    case 0x1B:
    case 0x2B:
    case 0x3B: {
        auto& reg = accessR16(mem, GB::Decode::decodeR16(opcode));
        reg = static_cast<AddressType>(reg - 1u);
        return finalizeFast(1);
    }
    case 0x08:
        write16(mem, fetchImm16(blocks.front(), 0), static_cast<AddressType>(getRegister(mem, GB::RegisterId::SP)->value));
        return finalizeFast(3);
    case 0x09:
    case 0x19:
    case 0x29:
    case 0x39: {
        auto& hl = getRegisterPair(mem, GB::RegisterId::HL)->value;
        const AddressType lhs = hl;
        const AddressType rhs = accessR16(mem, GB::Decode::decodeR16(opcode));
        hl = static_cast<AddressType>(lhs + rhs);
        const bool h = ((lhs & 0x0FFFu) + (rhs & 0x0FFFu)) > 0x0FFFu;
        const bool c = static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs) > 0xFFFFu;
        const bool z = (flags(mem) & kFlagZ) != 0;
        setFlags(mem, z, false, h, c);
        return finalizeFast(1);
    }
    case 0x18: {
        const int8_t offset = static_cast<int8_t>(fetchImm8(blocks.front(), 0));
        const auto target = static_cast<AddressType>(static_cast<int32_t>(feedback.pcBefore) + 2 + offset);
        updateControlFlowPc(mem, target, 2);
        return finalizeFast(2);
    }
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38: {
        if (conditionHolds(mem, GB::Decode::decodeCondition(opcode))) {
            const int8_t offset = static_cast<int8_t>(fetchImm8(blocks.front(), 0));
            const auto target = static_cast<AddressType>(static_cast<int32_t>(feedback.pcBefore) + 2 + offset);
            updateControlFlowPc(mem, target, 2);
        }
        return finalizeFast(2);
    }
    case 0xC3:
        updateControlFlowPc(mem, fetchImm16(blocks.front(), 0), 3);
        return finalizeFast(3);
    case 0xC2:
    case 0xCA:
    case 0xD2:
    case 0xDA:
        if (conditionHolds(mem, GB::Decode::decodeCondition(opcode))) {
            updateControlFlowPc(mem, fetchImm16(blocks.front(), 0), 3);
        }
        return finalizeFast(3);
    case 0xC9:
        updateControlFlowPc(mem, pop16(mem), 1);
        return finalizeFast(1);
    case 0xD9:
        updateControlFlowPc(mem, pop16(mem), 1);
        setIme(true);
        return finalizeFast(1);
    case 0xC0:
    case 0xC8:
    case 0xD0:
    case 0xD8:
        if (conditionHolds(mem, GB::Decode::decodeCondition(opcode))) {
            updateControlFlowPc(mem, pop16(mem), 1);
        }
        return finalizeFast(1);
    case 0xCD: {
        const AddressType target = fetchImm16(blocks.front(), 0);
        push16(mem, static_cast<AddressType>(feedback.pcBefore + 3u));
        updateControlFlowPc(mem, target, 3);
        return finalizeFast(3);
    }
    case 0xC4:
    case 0xCC:
    case 0xD4:
    case 0xDC:
        if (conditionHolds(mem, GB::Decode::decodeCondition(opcode))) {
            push16(mem, static_cast<AddressType>(feedback.pcBefore + 3u));
            updateControlFlowPc(mem, fetchImm16(blocks.front(), 0), 3);
        }
        return finalizeFast(3);
    case 0xC1:
    case 0xD1:
    case 0xE1:
    case 0xF1: {
        const auto target = GB::Decode::decodeR16Stack(opcode);
        AddressType value = pop16(mem);
        if (target == GB::Decode::R16Stack::AF) {
            value = static_cast<AddressType>(value & 0xFFF0u);
        }
        accessStackR16(mem, target) = value;
        return finalizeFast(1);
    }
    case 0xC5:
    case 0xD5:
    case 0xE5:
    case 0xF5:
        push16(mem, accessStackR16(mem, GB::Decode::decodeR16Stack(opcode)));
        return finalizeFast(1);
    case 0xE9:
        updateControlFlowPc(mem, getRegisterPair(mem, GB::RegisterId::HL)->value, 1);
        return finalizeFast(1);
    case 0xE0:
        write8(mem, static_cast<AddressType>(0xFF00u + fetchImm8(blocks.front(), 0)), accumulator(mem));
        return finalizeFast(2);
    case 0xF0:
        accumulator(mem) = read8(mem, static_cast<AddressType>(0xFF00u + fetchImm8(blocks.front(), 0)));
        return finalizeFast(2);
    case 0xE2:
        write8(mem, static_cast<AddressType>(0xFF00u + getRegisterPair(mem, GB::RegisterId::BC)->lo), accumulator(mem));
        return finalizeFast(1);
    case 0xF2:
        accumulator(mem) = read8(mem, static_cast<AddressType>(0xFF00u + getRegisterPair(mem, GB::RegisterId::BC)->lo));
        return finalizeFast(1);
    case 0xE8: {
        const int8_t offset = static_cast<int8_t>(fetchImm8(blocks.front(), 0));
        auto* sp = getRegister(mem, GB::RegisterId::SP);
        const AddressType original = sp->value;
        sp->value = static_cast<AddressType>(static_cast<int32_t>(original) + offset);
        const uint8_t low = static_cast<uint8_t>(original & 0xFFu);
        const uint8_t imm = static_cast<uint8_t>(offset);
        setFlags(mem, false, false, ((low & 0x0Fu) + (imm & 0x0Fu)) > 0x0Fu,
                 static_cast<uint16_t>(low) + static_cast<uint16_t>(imm) > 0xFFu);
        return finalizeFast(2);
    }
    case 0xF8: {
        const int8_t offset = static_cast<int8_t>(fetchImm8(blocks.front(), 0));
        const AddressType sp = static_cast<AddressType>(getRegister(mem, GB::RegisterId::SP)->value);
        const uint8_t low = static_cast<uint8_t>(sp & 0xFFu);
        const uint8_t imm = static_cast<uint8_t>(offset);
        getRegisterPair(mem, GB::RegisterId::HL)->value = static_cast<AddressType>(static_cast<int32_t>(sp) + offset);
        setFlags(mem, false, false, ((low & 0x0Fu) + (imm & 0x0Fu)) > 0x0Fu,
                 static_cast<uint16_t>(low) + static_cast<uint16_t>(imm) > 0xFFu);
        return finalizeFast(2);
    }
    case 0xF9:
        getRegister(mem, GB::RegisterId::SP)->value = getRegisterPair(mem, GB::RegisterId::HL)->value;
        return finalizeFast(1);
    case 0xEA:
        write8(mem, fetchImm16(blocks.front(), 0), accumulator(mem));
        return finalizeFast(3);
    case 0xFA:
        accumulator(mem) = read8(mem, fetchImm16(blocks.front(), 0));
        return finalizeFast(3);
    case 0xF3:
        setIme(false);
        return finalizeFast(1);
    case 0xFB:
        scheduleImeEnable();
        return finalizeFast(1);
    case 0x07:
    case 0x0F:
    case 0x17:
    case 0x1F: {
        auto& a = accumulator(mem);
        const bool carryIn = (flags(mem) & kFlagC) != 0;
        const DataType oldValue = a;
        switch ((opcode >> 3) & 0x03u) {
        case 0:
            a = static_cast<DataType>((a << 1) | (a >> 7));
            setFlags(mem, false, false, false, (oldValue & 0x80u) != 0);
            break;
        case 1:
            a = static_cast<DataType>((a >> 1) | (a << 7));
            setFlags(mem, false, false, false, (oldValue & 0x01u) != 0);
            break;
        case 2:
            a = static_cast<DataType>((a << 1) | (carryIn ? 1 : 0));
            setFlags(mem, false, false, false, (oldValue & 0x80u) != 0);
            break;
        case 3:
            a = static_cast<DataType>((a >> 1) | (carryIn ? 0x80u : 0u));
            setFlags(mem, false, false, false, (oldValue & 0x01u) != 0);
            break;
        }
        return finalizeFast(1);
    }
    case 0x27: {
        auto& a = accumulator(mem);
        const DataType flagReg = flags(mem);
        const bool n = (flagReg & kFlagN) != 0;
        const bool h = (flagReg & kFlagH) != 0;
        const bool c = (flagReg & kFlagC) != 0;
        DataType adjust = 0;
        bool carry = c;
        if (!n) {
            if (h || (a & 0x0Fu) > 0x09u) adjust |= 0x06u;
            if (c || a > 0x99u) { adjust |= 0x60u; carry = true; }
            a = static_cast<DataType>(a + adjust);
        } else {
            if (h) adjust |= 0x06u;
            if (c) adjust |= 0x60u;
            a = static_cast<DataType>(a - adjust);
        }
        flags(mem) = static_cast<DataType>(((a == 0) ? kFlagZ : 0) | (n ? kFlagN : 0) | (carry ? kFlagC : 0));
        return finalizeFast(1);
    }
    case 0x2F:
        accumulator(mem) = static_cast<DataType>(~accumulator(mem));
        flags(mem) = static_cast<DataType>((flags(mem) & (kFlagZ | kFlagC)) | kFlagN | kFlagH);
        return finalizeFast(1);
    case 0x37:
        flags(mem) = static_cast<DataType>((flags(mem) & kFlagZ) | kFlagC);
        return finalizeFast(1);
    case 0x3F: {
        const bool carry = (flags(mem) & kFlagC) == 0;
        flags(mem) = static_cast<DataType>(flags(mem) & kFlagZ);
        if (carry) flags(mem) = static_cast<DataType>(flags(mem) | kFlagC);
        return finalizeFast(1);
    }
    case 0xC6:
    case 0xCE:
    case 0xD6:
    case 0xDE:
    case 0xE6:
    case 0xEE:
    case 0xF6:
    case 0xFE:
        executeMathOp(opcode, fetchImm8(blocks.front(), 0));
        return finalizeFast(2);
    default:
        break;
    }

    if ((opcode & 0xC7u) == 0x04u) {
        const auto reg = GB::Decode::decodeR8Dest(opcode);
        const DataType oldValue = readR8(mem, reg);
        const DataType newValue = static_cast<DataType>(oldValue + 1u);
        writeR8(mem, reg, newValue);
        const bool c = (flags(mem) & kFlagC) != 0;
        setFlags(mem, newValue == 0, false, ((oldValue & 0x0Fu) + 1u) > 0x0Fu, c);
        return finalizeFast(1);
    }

    if ((opcode & 0xC7u) == 0x05u) {
        const auto reg = GB::Decode::decodeR8Dest(opcode);
        const DataType oldValue = readR8(mem, reg);
        const DataType newValue = static_cast<DataType>(oldValue - 1u);
        writeR8(mem, reg, newValue);
        const bool c = (flags(mem) & kFlagC) != 0;
        setFlags(mem, newValue == 0, true, (oldValue & 0x0Fu) == 0, c);
        return finalizeFast(1);
    }

    if ((opcode & 0xC7u) == 0x06u) {
        writeR8(mem, GB::Decode::decodeR8Dest(opcode), fetchImm8(blocks.front(), 0));
        return finalizeFast(2);
    }

    if (opcode == 0x02 || opcode == 0x12 || opcode == 0x22 || opcode == 0x32 ||
        opcode == 0x0A || opcode == 0x1A || opcode == 0x2A || opcode == 0x3A) {
        auto* hl = getRegisterPair(mem, GB::RegisterId::HL);
        AddressType address = 0;
        switch (opcode) {
        case 0x02:
        case 0x0A:
            address = getRegisterPair(mem, GB::RegisterId::BC)->value;
            break;
        case 0x12:
        case 0x1A:
            address = getRegisterPair(mem, GB::RegisterId::DE)->value;
            break;
        default:
            address = hl->value;
            break;
        }
        if ((opcode & 0x08u) == 0) {
            write8(mem, address, accumulator(mem));
        } else {
            accumulator(mem) = read8(mem, address);
        }
        if (opcode == 0x22 || opcode == 0x2A) {
            hl->value = static_cast<AddressType>(hl->value + 1u);
        } else if (opcode == 0x32 || opcode == 0x3A) {
            hl->value = static_cast<AddressType>(hl->value - 1u);
        }
        return finalizeFast(1);
    }

    return false;
}

void LR3592_DMG::execute(const BMMQ::executionBlock<AddressType, DataType, AddressType>& block, BMMQ::fetchBlock<AddressType, DataType>& fb)
{
    feedback.pcBefore = (pcRegister_ != nullptr)
        ? static_cast<uint32_t>(pcRegister_->value)
        : 0;

    feedback.isControlFlow = false;
    std::size_t executedByteCount = 0;
    const auto& blocks = fb.getblockData();
    if (!blocks.empty() && !blocks.front().data.empty()) {
        feedback.isControlFlow = isControlFlowOpcode(blocks.front().data[0]);
    }
    for (const auto& dataBlock : blocks) {
        executedByteCount += dataBlock.data.size();
    }
    feedback.segmentBoundaryHint = feedback.isControlFlow;
    feedback.executionPath = BMMQ::ExecutionPathHint::CanonicalFetchDecodeExecute;

    auto* snapshot = block.getSnapshot();
    if (snapshot == nullptr) return;

    for (const auto& step : block.getSteps()) {
        step(*snapshot, fb);
    }

    std::size_t pcAdvance = executedByteCount;
    if (haltBugPcAdjustPending && pcAdvance > 0) {
        --pcAdvance;
    }
    haltBugPcAdjustPending = false;

    if (pcRegister_ != nullptr) {
        pcRegister_->value = static_cast<AddressType>(
            pcRegister_->value + pcAdvance);
        feedback.pcAfter = static_cast<uint32_t>(pcRegister_->value);
    } else {
        feedback.pcAfter = feedback.pcBefore;
    }

    std::size_t retiredCycles = pendingCycleCharge_;
    pendingCycleCharge_ = 0;
    if (retiredCycles == 0) {
        if (executedByteCount == 0 && (haltFlag || stopFlag || dmaActive)) {
            retiredCycles = 4u;
        } else {
            retiredCycles = instructionCyclesFor(
                fb,
                static_cast<AddressType>(feedback.pcBefore),
                static_cast<AddressType>(feedback.pcAfter));
        }
    }
    retireInstruction(retiredCycles);
}

const BMMQ::CpuFeedback& LR3592_DMG::getLastFeedback() const
{
    return feedback;
}

void LR3592_DMG::setIme(bool enabled)
{
    ime = enabled;
    imeEnablePending = false;
    imeEnableDelay = 0;
}

void LR3592_DMG::scheduleImeEnable()
{
    imeEnablePending = true;
    imeEnableDelay = 1;
}

void LR3592_DMG::resetDivider()
{
    dividerCounter = 0;
    writeIoRegister("DIV", 0);
}

AddressType LR3592_DMG::normalizeAccessAddress(AddressType address)
{
    if (address >= 0xE000 && address <= 0xFDFF) {
        return static_cast<AddressType>(address - 0x2000);
    }
    return address;
}

bool LR3592_DMG::lcdEnabled() const
{
    return (readCachedRegister(hardwareRegisters_.lcdc) & 0x80u) != 0;
}

DataType LR3592_DMG::currentPpuMode() const
{
    if (!lcdEnabled()) {
        return 0;
    }

    const auto ly = readCachedRegister(hardwareRegisters_.ly);
    if (ly >= 144u) {
        return 1;
    }

    const auto lineDot = static_cast<uint16_t>(ppuDotCounter % 456u);
    if (lineDot < 80u) {
        return 2;
    }
    if (lineDot < 252u) {
        return 3;
    }
    return 0;
}

bool LR3592_DMG::handleMemoryRead(AddressType address, std::span<DataType> value) const
{
    address = normalizeAccessAddress(address);
    if (address == 0xFF00 && !value.empty()) {
        value[0] = static_cast<DataType>(0xC0u | joypSelect | joypadLowNibble());
        return true;
    }
    if (address >= 0xFEA0 && address <= 0xFEFF) {
        std::fill(value.begin(), value.end(), static_cast<DataType>(0xFF));
        return true;
    }

    if (dmaActive && !isHramAddress(address)) {
        std::fill(value.begin(), value.end(), static_cast<DataType>(0xFF));
        return true;
    }

    if (value.size() == 1) {
        if (const auto* slot = cachedIoRegisterForAddress(address); slot != nullptr) {
            value[0] = readCachedRegister(*slot);
            return true;
        }
    }

    if (lcdEnabled()) {
        const auto mode = currentPpuMode();
        if (address >= 0x8000 && address <= 0x9FFF && mode == 3) {
            std::fill(value.begin(), value.end(), static_cast<DataType>(0xFF));
            return true;
        }
        if (address >= 0xFE00 && address <= 0xFE9F && (mode == 2 || mode == 3)) {
            std::fill(value.begin(), value.end(), static_cast<DataType>(0xFF));
            return true;
        }
    }

    return false;
}

bool LR3592_DMG::handleMemoryWrite(AddressType address, std::span<const DataType> value)
{
    address = normalizeAccessAddress(address);
    if (value.size() == 1 && address == 0xFF00) {
        const DataType oldLow = joypadLowNibble();
        joypSelect = static_cast<DataType>(value[0] & 0x30u);
        const DataType newLow = joypadLowNibble();
        writeIoRegister("JOYP", static_cast<DataType>(0xC0u | joypSelect | newLow));
        if ((oldLow & static_cast<DataType>(~newLow)) != 0) {
            requestInterrupt(kInterruptJoypad);
        }
        return true;
    }
    if (value.size() == 1 && address == 0xFF02) {
        const DataType control = value[0];
        writeIoRegister("SC", control);
        if ((control & 0x80u) != 0) {
            serialTransferActive = true;
            serialCycleProgress = 0;
        } else {
            serialTransferActive = false;
            serialCycleProgress = 0;
        }
        return true;
    }
    if (value.size() == 1 && address == 0xFF04) {
        resetDivider();
        return true;
    }
    if (value.size() == 1 && address == 0xFF41) {
        const DataType current = readCachedRegister(hardwareRegisters_.stat);
        const DataType writableBits = static_cast<DataType>(value[0] & 0x78u);
        const DataType readOnlyBits = static_cast<DataType>(current & 0x07u);
        writeIoRegister("STAT", static_cast<DataType>(0x80u | writableBits | readOnlyBits));
        return true;
    }
    if (value.size() == 1 && address == 0xFF44) {
        ppuDotCounter = 0;
        const DataType lyc = readCachedRegister(hardwareRegisters_.lyc);
        const DataType mode = lcdEnabled() ? 0x02u : 0x00u;
        DataType stat = static_cast<DataType>((readCachedRegister(hardwareRegisters_.stat) & 0x78u) | mode | 0x80u);
        if (lyc == 0u) {
            stat = static_cast<DataType>(stat | 0x04u);
        } else {
            stat = static_cast<DataType>(stat & ~0x04u);
        }
        writeIoRegister("LY", 0x00u);
        writeIoRegister("STAT", stat);
        statInterruptLatched = false;
        return true;
    }
    if (value.size() == 1 && address == 0xFF46) {
        const DataType highByte = value[0];
        writeIoRegister("DMA", highByte);
        dmaActive = true;
        dmaSourceBase = static_cast<AddressType>(highByte << 8);
        dmaCycleProgress = 0;
        return true;
    }
    if (value.size() == 1 && ((address >= 0xFF10u && address <= 0xFF26u) || (address >= 0xFF30u && address <= 0xFF3Fu))) {
        handleApuRegisterWrite(address, value[0]);
        return true;
    }
    if (address >= 0xFEA0 && static_cast<std::size_t>(address - 0xFEA0u) + value.size() <= 0x60u) {
        return true;
    }

    if (dmaActive && !isHramAddress(address)) {
        return true;
    }

    if (lcdEnabled()) {
        const auto mode = currentPpuMode();
        if (address >= 0x8000 && address <= 0x9FFF && mode == 3) {
            return true;
        }
        if (address >= 0xFE00 && address <= 0xFE9F && (mode == 2 || mode == 3)) {
            return true;
        }
    }

    return false;
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

    setOpcode(0xD9, emitStep(1, [this](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([this](auto& snapshot, auto&) {
            updateControlFlowPc(snapshot, pop16(snapshot), 1);
            setIme(true);
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

    setOpcode(0xF3, emitStep(1, [this](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([this](auto&, auto&) {
            setIme(false);
        });
    }));

    setOpcode(0xFB, emitStep(1, [this](auto& block, const auto&, std::size_t, DataType) {
        block.addStep([this](auto&, auto&) {
            scheduleImeEnable();
        });
    }));

}
