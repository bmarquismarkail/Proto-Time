cmake_minimum_required(VERSION 3.10)

project(time)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(emulator IMicrocode.cpp IOpcode.cpp executionBlock.cpp cores/gameboy.cpp cores/gameboy_opcodes.cpp emulator.cpp)