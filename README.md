# Project T.I.M.E

Welcome to T.I.M.E: The Infinite Modder's Emulator. As of right now(23 Oct, 2020), it is in its pre-alpha stage

## Build Instructions

In this pre-alpha stage, there is no build automation, so to build, either make a project of your choice or on the command line, type:

`$ g++ -o ./bin/emulator -std=c++17 emulator.cpp`

##Installation

There are no installation instructions.

## The API
### Structures
#### fetchBlockData
``` template<typename AddressType, typename DataType>
struct fetchBlockData```

#### fetchBlock
``` template<typename AddressType, typename DataType>
class fetchBlock```
