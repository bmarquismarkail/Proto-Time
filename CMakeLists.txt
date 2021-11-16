cmake_minimum_required(VERSION 3.10)

project(time)

set (CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)
set (CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(-Wall -Wextra )
add_executable(emulator cores/gameboy.cpp cores/gameboy_opcodes.cpp emulator.cpp)
