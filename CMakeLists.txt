cmake_minimum_required(VERSION 3.10)

project(timeEmulator)

set (CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)
set (CMAKE_CXX_STANDARD_REQUIRED True)
include_directories(templ memory cores)
add_compile_options(-Wall -Wextra )
add_library(time-gameboy-interpreter-core
	cores/gameboy/decode/gb_decoder.cpp
	cores/gameboy/decode/gb_interpreter.cpp
	cores/gameboy/gameboy_opcodes.cpp
	cores/gameboy/gameboy.cpp
)
add_executable(timeEmulator emulator.cpp)
target_link_libraries(timeEmulator time-gameboy-interpreter-core)
