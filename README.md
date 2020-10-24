# Project T.I.M.E

Welcome to T.I.M.E: The Infinite Modder's Emulator. As of right now(23 Oct, 2020), it is in its pre-alpha stage

## Build Instructions

In this pre-alpha stage, there is no build automation, so to build, either make a project of your choice or on the command line, type:

`$ g++ -o ./bin/emulator -std=c++17 emulator.cpp`

## Installation

There are no installation instructions.

## The API
### Structures
#### Fetch/Decode/Execute
##### fetchBlockData
`template<typename AddressType, typename DataType> struct fetchBlockData`

The fetchBlockData struct is variable that stores an address offset and a dynamic array of raw execution data.

Members:

- offset:<br>
The Offset of the data, which coincides with a fetchBlock.
- data:<br>
Raw execution data.

---

##### fetchBlock
`template<typename AddressType, typename DataType> class fetchBlock`

An entire fetchblock.

Members:
- baseAddress:<br>
> > The starting address of the fetchBlock
- store:<br>
> > A struct holding changes in memory while executing data in the block
- baseRegister:<br>
> > The initial register state
- file:<br>
> > The current register state of the block, which starts with the same values as baseRegister
- blockData:<br>
> > A dynamic array of fetchBlockData types

Functions:
- void setbaseAddress(AddressType address)<br>
> > Sets the base address of the fetchBlock
- AddressType getbaseAddress() const<br>
> > Gets the base address of the fetchBlock
- std::vector<fetchBlockData<AddressType, DataType>> &getblockData()<br>
> > returns a reference of blockData
- void setRegisterFile(RegisterFile<AddressType> rf)<br>
> > sets baseRegister into value rf
- CPU_Register<AddressType> \*getRegisterAt(const std::string id, AddressType offset, RegisterFile<AddressType> mainFile )
> > returns the register identified with string id with value at offset on either file, baseRegister or mainFile, if neither has it
---

##### Imicrocode
`class Imicrocode`

The microcode interface.

A microcode is the smallest function needed. Opcodes are made with multiple microcode

---

##### IOpcode
`class IOpcode`

---

#### Memory
##### MemoryPool
`template<typename AddressType, typename DataType> class MemoryPool`

---

#### The CPU
`template<typename AddressType, typename DataType> class CPU`