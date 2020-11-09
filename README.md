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
- baseAddress:  
The starting address of the fetchBlock
- store:  
A struct holding changes in memory while executing data in the block
- baseRegister:  
The initial register state
- file:  
The current register state of the block, which starts with the same values as baseRegister
- blockData:  
A dynamic array of fetchBlockData types

Functions:
- void setbaseAddress(AddressType address)  
Sets the base address of the *fetchBlock*
- AddressType getbaseAddress() const  
Gets the base address of the *fetchBlock*
- std::vector<fetchBlockData<AddressType, DataType>> &getblockData()  
returns a reference of *blockData*
- void setRegisterFile(RegisterFile<AddressType> rf)  
sets *baseRegister* into value *rf*
- CPU_Register<AddressType> \*getRegisterAt(const std::string id, AddressType offset, RegisterFile<AddressType> mainFile )  
returns the register identified with string id with value at offset on either file, baseRegister or mainFile, if neither has it
---
##### microcodefunc
`using microcodeFunc = std::function<void()>`  
a microcode function.

##### Imicrocode
`class Imicrocode`

The Microcode interface.

This type hold multiple *microcodefunc*'s instances. this allows one microcode to be a combination of functions.

Members:
- static std::map< std::string, microcodeFunc  > v  
A map of string keys to *microcodeFunc*'s

Functions:
- const microcodeFunc \*findMicrocode(const std::string id)  
searches for microcode identified by string *id*
- void registerMicrocode(const std::string id, microcodeFunc func)  
inserts function *func* into *v* with string key *id*

---

##### IOpcode
`class IOpcode`

The Opcode Interface

This holds multiple *Imicrocode*'s, which in turn creates a CPU operation. Like a *Imicrocode* is able to obtain multiple *microcodeFunc*'s, an *IOpcode* can have multiple *Imicrocode*'s

Members:
- std::vector<const microcodeFunc *> microcode  
A dynamic array of *microcodeFunc*'s  
> **TODO**: implement this as a vector of *Imicrocode*'s

Functions:
- IOpcode(Imicrocode& library, const std::string id)  
Creates an IOpcode, adding microcode in *library* with string *id*  
> **TODO**: this needs to add ALL of *library* after above is implementated
- IOpcode(const microcodeFunc  \*func)  
Creates an IOpcode, adding microcodeFunc pointer *\*func* into it  
> **TODO**: Remove this when above is implemented
- IOpcode(std::initializer_list<const microcodeFunc  *> list): microcode(list)  
Creates an IOpcode, adding *list* to vector  
> **TODO**: Remove this when above is implemented
- void push_microcode(const microcodeFunc  *func)  
Adds microcodeFunc pointer *\*func* in *microcode*
> **TODO**: Change *func* from *microcodeFunc* to *Imicrocode* after above is implemented
- void push_microcode(Imicrocode& library, const std::string id)  
Adds microcode in *library* with string *id*<br>
> **TODO**: this needs to add ALL of *library* after above is implementated
- template<typename AddressType, typename DataType>
void operator()(fetchBlock<AddressType, DataType> &fb)  
Executes object by iterating *microcode* and invoking their function call

---

#### Memory
##### memAccess

An enumerator that indicates how a certain part of the memory pool can be accessed
- 0: No Access
- 1: Read Access
- 2: Write Access
- 3: Read and Write Access

##### MemoryPool
`template<typename AddressType, typename DataType> class MemoryPool`

The MemoryPool. This type will hold the entire memory structure of the CPU being emulated, as well as establish read/write access

Members:
- std::vector<std::tuple<AddressType, AddressType, memAccess>> memoryMap  
A vector of 3-member tuples:  
  - The Absolute Beginning of the Map, represented by an AddressType  
  - The Absolute End of the Map, represented by an AddressType  
  - And Read/Write Access, represented by an memAccess emumeration  
- std::vector<DataType> mem  
The Raw memory array of *DataType\**s

---

#### The CPU
`template<typename AddressType, typename DataType> class CPU`

The CPU class

Functions:
- virtual fetchBlock<AddressType, DataType> fetch()=0  
Fetches Data and creates a fetchBlock instance.
- virtual executionBlock decode(OpcodeList &oplist, fetchBlock<AddressType, DataType>& fetchData)=0
Using an OpcodeList reference *&oplist* and fetchBlock reference \*&fetchData*, creates an executionBlock with it
- virtual void execute(const executionBlock& block, fetchBlock<AddressType, DataType> &fb)=0
Executes all opcodes in reference *&block* using data from *&fb*

---

## Implementing a New Core

To implement a core, you make a class derived from the BMMG::CPU class, and define its fetch, decode, and execute function

Unless needed, the fetch() function usually requires reading from memory, so the MemoryPool class was made to make contiguous memory, and a fetchBlock class to hold raw, undecode data from the memory pool

The decode() function will most likely be the function with the most deviation, because it requires architecture-specific code. However, the end result is the same: a list of *IOpcode*s 