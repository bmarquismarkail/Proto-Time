cmake_minimum_required(VERSION 3.10)

project(timeEmulator)

set (CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set (CMAKE_CXX_STANDARD_REQUIRED True)
include_directories(. templ memory cores)
add_compile_options(-Wall -Wextra )
add_library(time-gameboy-interpreter-core
	cores/gameboy/decode/gb_decoder.cpp
	cores/gameboy/decode/gb_interpreter.cpp
	cores/gameboy/gameboy.cpp
)
add_executable(timeEmulator emulator.cpp)
target_link_libraries(timeEmulator time-gameboy-interpreter-core)

enable_testing()
add_executable(time-smoke-snapshot tests/smoke_snapshot.cpp)
add_test(NAME smoke_snapshot COMMAND time-smoke-snapshot)
add_executable(time-smoke-register-snapshot tests/smoke_register_snapshot.cpp)
add_test(NAME smoke_register_snapshot COMMAND time-smoke-register-snapshot)
add_executable(time-smoke-instruction-cycle tests/smoke_instruction_cycle.cpp)
target_link_libraries(time-smoke-instruction-cycle time-gameboy-interpreter-core)
add_test(NAME smoke_instruction_cycle COMMAND time-smoke-instruction-cycle)
add_executable(time-smoke-executor tests/smoke_executor.cpp)
target_link_libraries(time-smoke-executor time-gameboy-interpreter-core)
add_test(NAME smoke_executor COMMAND time-smoke-executor)
