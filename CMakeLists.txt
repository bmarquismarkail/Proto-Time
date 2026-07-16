cmake_minimum_required(VERSION 3.10)

project(timeEmulator)

option(BMMQ_ENABLE_TSAN "Enable ThreadSanitizer for race detection builds" OFF)
option(BMMQ_PROFILE_VDP_RENDER "Enable profiling-oriented compile flags for GameGearVDP::renderFramePixels attribution" OFF)
option(PROTO_TIME_BUILD_SDL_FRONTEND "Build the optional SDL frontend module" ON)
option(PROTO_TIME_BUILD_SDL_AUDIO "Build the optional SDL audio-output module" ON)
option(PROTO_TIME_BUILD_GLFW_FRONTEND "Build the optional GLFW frontend module" ON)

if (PROTO_TIME_BUILD_SDL_FRONTEND OR PROTO_TIME_BUILD_SDL_AUDIO)
    find_package(SDL2 QUIET)
endif()
if (PROTO_TIME_BUILD_GLFW_FRONTEND)
    find_package(glfw3 3.3 QUIET)
endif()
if (PROTO_TIME_BUILD_GLFW_FRONTEND AND (TARGET glfw OR TARGET glfw3::glfw))
    set(OpenGL_GL_PREFERENCE LEGACY)
    find_package(OpenGL QUIET)
endif()
find_package(ZLIB REQUIRED)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

set (CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set (CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories(. templ memory cores)
add_compile_options(-Wall -Wextra )

add_library(time-simd-pixel-ops STATIC
    machine/plugins/video/SimdPixelOps.cpp
)
set_target_properties(time-simd-pixel-ops PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(time-simd-pixel-ops-scalar STATIC
    machine/plugins/video/SimdPixelOps.cpp
)
target_compile_definitions(time-simd-pixel-ops-scalar PRIVATE BMMQ_SIMD_FORCE_SCALAR=1)
set_target_properties(time-simd-pixel-ops-scalar PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (BMMQ_ENABLE_TSAN)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=thread")
        add_compile_definitions(BMMQ_TSAN_ENABLED=1)
    else()
        message(FATAL_ERROR "BMMQ_ENABLE_TSAN=ON requires Clang or GCC")
    endif()
endif()
add_library(time-machine-runtime-common STATIC
    inst_cycle/executor/ExecutorPolicyRegistry.cpp
    machine/MachineLifecycleCoordinator.cpp
    machine/TimingService.cpp
    machine/VisualCaptureWriter.cpp
    machine/VisualOverrideService.cpp
    machine/VisualPackManifest.cpp
    machine/PngDecode.cpp
    machine/ImageDecoder.cpp
    machine/DebugSnapshotService.cpp
    machine/SaveState.cpp
)
set_target_properties(time-machine-runtime-common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(time-machine-runtime-common PUBLIC ZLIB::ZLIB time-simd-pixel-ops)

add_library(time-gameboy-interpreter-core
	cores/gameboy/decode/gb_decoder.cpp
	cores/gameboy/decode/gb_interpreter.cpp
    cores/gameboy/gameboy.cpp
    cores/gameboy/GameBoyIrExecution.cpp
    cores/gameboy/GameBoyNativeExecution.cpp
    cores/gameboy/GameBoyMachine.cpp
    cores/gameboy/GameBoyMemoryMap.cpp
    cores/gameboy/GameBoyPPU.cpp
    cores/gameboy/GameBoyAPU.cpp
    cores/gameboy/GameBoyInput.cpp
    cores/gameboy/GameBoyMapper.cpp
    cores/gameboy/dma_controller.cpp
    cores/gameboy/vram_manager.cpp
)

# Sega Game Gear core (WIP)
add_library(time-gamegear-core STATIC
    cores/gamegear/GameGearMachine.cpp
    cores/gamegear/Z80Interpreter.cpp
    cores/gamegear/GameGearVDP.cpp
    cores/gamegear/GameGearPSG.cpp
    cores/gamegear/GameGearInput.cpp
    cores/gamegear/GameGearCartridge.cpp
    cores/gamegear/GameGearMapperFactory.cpp
    cores/gamegear/GameGearMemoryMap.cpp
)
set_target_properties(time-gamegear-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(time-gamegear-core PUBLIC time-machine-runtime-common)
if (BMMQ_PROFILE_VDP_RENDER)
    target_compile_definitions(time-gamegear-core PRIVATE BMMQ_PROFILE_VDP_RENDER=1)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(
            cores/gamegear/GameGearVDP.cpp
            PROPERTIES
                COMPILE_OPTIONS "-fno-omit-frame-pointer;-g;-fno-inline-functions"
        )
        message(STATUS "BMMQ_PROFILE_VDP_RENDER=ON: applying profiling flags to cores/gamegear/GameGearVDP.cpp")
    else()
        message(WARNING "BMMQ_PROFILE_VDP_RENDER=ON requested, but compiler is not Clang/GNU; no per-file profiling flags applied")
    endif()
endif()

set_target_properties(time-gameboy-interpreter-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(time-gameboy-interpreter-core PUBLIC time-machine-runtime-common)
add_library(time-dynamic-plugin-loader
    machine/plugins/DynamicPluginModule.cpp
)
target_link_libraries(time-dynamic-plugin-loader PUBLIC time-machine-runtime-common ${CMAKE_DL_LIBS})
add_library(time-frontend-loader
    machine/plugins/FrontendPluginLoader.cpp
)
target_link_libraries(time-frontend-loader PUBLIC time-dynamic-plugin-loader)
add_library(time-audio-output-loader
    machine/plugins/AudioOutputPluginLoader.cpp
)
target_link_libraries(time-audio-output-loader PUBLIC time-dynamic-plugin-loader)
add_library(time-audio-transport-plugin
    machine/plugins/audio/AudioTransportPlugin.cpp)
target_link_libraries(time-audio-transport-plugin PUBLIC time-machine-runtime-common)
add_library(time-emulator-config STATIC
    emulator/EmulatorConfig.cpp
)
add_library(time-machine-factory STATIC
    emulator/MachineFactory.cpp
)
target_link_libraries(time-machine-factory
    PUBLIC
        time-machine-runtime-common
        time-gameboy-interpreter-core
        time-gamegear-core
)
target_link_libraries(time-emulator-config PUBLIC time-machine-factory time-dynamic-plugin-loader)
add_library(time-emulator-host STATIC
    emulator/EmulatorHost.cpp
)
target_link_libraries(time-emulator-host
    PUBLIC
        time-machine-factory
        time-emulator-config
)
add_library(time-dummy-audio-output STATIC
    machine/plugins/audio_output/DummyAudioOutput.cpp
)
add_library(time-file-audio-output STATIC
    machine/plugins/audio_output/FileAudioOutput.cpp
)
add_library(time-headless-video-adapter STATIC
    machine/plugins/video/adapters/HeadlessFrameDumper.cpp
)
set_target_properties(time-dummy-audio-output PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(time-file-audio-output PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(time-headless-video-adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_executable(timeEmulator emulator.cpp)
target_link_libraries(timeEmulator PRIVATE time-emulator-host time-frontend-loader
    time-audio-output-loader time-audio-transport-plugin
    time-dummy-audio-output time-file-audio-output)

if (PROTO_TIME_BUILD_SDL_FRONTEND)
    add_library(time-sdl-frontend-plugin SHARED
        machine/plugins/sdl_frontend/SdlFrontendModule.cpp)
    set(BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE 0)
    if (TARGET SDL2::SDL2)
        target_link_libraries(time-sdl-frontend-plugin PRIVATE SDL2::SDL2)
        set(BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE 1)
    elseif (SDL2_FOUND)
        target_include_directories(time-sdl-frontend-plugin PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(time-sdl-frontend-plugin PRIVATE ${SDL2_LIBRARIES})
        set(BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE 1)
    endif()
    target_compile_definitions(time-sdl-frontend-plugin PRIVATE
        BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL=${BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE})
endif()

if (PROTO_TIME_BUILD_SDL_AUDIO)
    add_library(time-sdl-audio-output-plugin SHARED
        machine/plugins/sdl_audio_output/SdlAudioOutputModule.cpp)
    set(BMMQ_SDL_AUDIO_COMPILED_WITH_SDL_VALUE 0)
    if (TARGET SDL2::SDL2)
        target_link_libraries(time-sdl-audio-output-plugin PRIVATE SDL2::SDL2)
        set(BMMQ_SDL_AUDIO_COMPILED_WITH_SDL_VALUE 1)
    elseif (SDL2_FOUND)
        target_include_directories(time-sdl-audio-output-plugin PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(time-sdl-audio-output-plugin PRIVATE ${SDL2_LIBRARIES})
        set(BMMQ_SDL_AUDIO_COMPILED_WITH_SDL_VALUE 1)
    endif()
    target_compile_definitions(time-sdl-audio-output-plugin PRIVATE
        BMMQ_SDL_AUDIO_COMPILED_WITH_SDL=${BMMQ_SDL_AUDIO_COMPILED_WITH_SDL_VALUE})
endif()

if (PROTO_TIME_BUILD_GLFW_FRONTEND)
    add_library(time-glfw-frontend-plugin SHARED
        machine/plugins/glfw_frontend/GlfwFrontendModule.cpp)
endif()
set(BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW_VALUE 0)
if (TARGET time-glfw-frontend-plugin AND (TARGET glfw OR TARGET glfw3::glfw) AND TARGET OpenGL::GL)
    if (TARGET glfw)
        target_link_libraries(time-glfw-frontend-plugin PRIVATE glfw)
    else()
        target_link_libraries(time-glfw-frontend-plugin PRIVATE glfw3::glfw)
    endif()
    target_link_libraries(time-glfw-frontend-plugin PRIVATE OpenGL::GL)
    set(BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW_VALUE 1)
endif()
if (TARGET time-glfw-frontend-plugin)
    target_compile_definitions(time-glfw-frontend-plugin PRIVATE
        BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW=${BMMQ_GLFW_FRONTEND_COMPILED_WITH_GLFW_VALUE})
endif()

enable_testing()
add_test(NAME smoke-gameboy-corpus-tool
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_gameboy_corpus.py)
add_executable(time-smoke-gamegear-core tests/smoke_gamegear_core.cpp)
target_link_libraries(time-smoke-gamegear-core time-gamegear-core)
add_test(NAME smoke-gamegear-core COMMAND time-smoke-gamegear-core)
add_executable(time-smoke-gamegear-cartridge
    tests/smoke_gamegear_cartridge.cpp
    cores/gamegear/GameGearCartridge.cpp
)
add_test(NAME smoke-gamegear-cartridge COMMAND time-smoke-gamegear-cartridge)
add_executable(time-smoke-gamegear-banking tests/smoke_gamegear_banking.cpp)
target_link_libraries(time-smoke-gamegear-banking time-gamegear-core)
add_test(NAME smoke-gamegear-banking COMMAND time-smoke-gamegear-banking)
add_executable(time-smoke-gamegear-machine-banking tests/smoke_gamegear_machine_banking.cpp)
target_link_libraries(time-smoke-gamegear-machine-banking time-gamegear-core)
add_test(NAME smoke-gamegear-machine-banking COMMAND time-smoke-gamegear-machine-banking)

add_executable(time-smoke-gamegear-vblank-irq tests/smoke_gamegear_vblank_irq.cpp)
target_link_libraries(time-smoke-gamegear-vblank-irq time-gamegear-core)
add_test(NAME smoke-gamegear-vblank-irq COMMAND time-smoke-gamegear-vblank-irq)

add_executable(time-probe-gamegear-wait-loop EXCLUDE_FROM_ALL tests/probe_gamegear_wait_loop.cpp)
target_link_libraries(time-probe-gamegear-wait-loop time-gamegear-core)

add_executable(time-smoke-ei-reti tests/smoke_ei_reti.cpp)
target_link_libraries(time-smoke-ei-reti time-gamegear-core)
add_test(NAME smoke-ei-reti COMMAND time-smoke-ei-reti)

add_executable(time-smoke-gamegear-z80-baseline tests/smoke_gamegear_z80_baseline.cpp)
target_link_libraries(time-smoke-gamegear-z80-baseline time-gamegear-core)
add_test(NAME smoke-gamegear-z80-baseline COMMAND time-smoke-gamegear-z80-baseline)

add_executable(time-smoke-gamegear-z80-extended tests/smoke_gamegear_z80_extended.cpp)
target_link_libraries(time-smoke-gamegear-z80-extended time-gamegear-core)
add_test(NAME smoke-gamegear-z80-extended COMMAND time-smoke-gamegear-z80-extended)

add_executable(time-smoke-gamegear-z80-halt tests/smoke_gamegear_z80_halt.cpp)
target_link_libraries(time-smoke-gamegear-z80-halt time-gamegear-core)
add_test(NAME smoke-gamegear-z80-halt COMMAND time-smoke-gamegear-z80-halt)

add_executable(time-smoke-gamegear-z80-interrupts tests/smoke_gamegear_z80_interrupts.cpp)
target_link_libraries(time-smoke-gamegear-z80-interrupts time-gamegear-core)
add_test(NAME smoke-gamegear-z80-interrupts COMMAND time-smoke-gamegear-z80-interrupts)

# Focused FD-indexed behavior tests
add_executable(time-smoke-gamegear-z80-fd-indexed tests/smoke_gamegear_z80_fd_indexed.cpp)
target_link_libraries(time-smoke-gamegear-z80-fd-indexed time-gamegear-core)
add_test(NAME smoke-gamegear-z80-fd-indexed COMMAND time-smoke-gamegear-z80-fd-indexed)

add_executable(time-smoke-gamegear-system-ports tests/smoke_gamegear_system_ports.cpp)
target_link_libraries(time-smoke-gamegear-system-ports time-gamegear-core)
add_test(NAME smoke-gamegear-system-ports COMMAND time-smoke-gamegear-system-ports)

add_executable(time-smoke-gamegear-audio tests/smoke_gamegear_audio.cpp)
target_link_libraries(time-smoke-gamegear-audio time-gamegear-core)
add_test(NAME smoke-gamegear-audio COMMAND time-smoke-gamegear-audio)

add_executable(time-smoke-gamegear-mapper-subsystem tests/smoke_gamegear_mapper_subsystem.cpp)
target_link_libraries(time-smoke-gamegear-mapper-subsystem time-gamegear-core)
add_test(NAME smoke-gamegear-mapper-subsystem COMMAND time-smoke-gamegear-mapper-subsystem)

add_executable(time-smoke-gamegear-3155235-reset tests/smoke_gamegear_3155235_reset.cpp)
target_link_libraries(time-smoke-gamegear-3155235-reset time-gamegear-core)
add_test(NAME smoke-gamegear-3155235-reset COMMAND time-smoke-gamegear-3155235-reset)

add_executable(time-smoke-gamegear-mapper-table tests/smoke_gamegear_mapper_table.cpp)
target_link_libraries(time-smoke-gamegear-mapper-table time-gamegear-core)
add_test(NAME smoke-gamegear-mapper-table COMMAND time-smoke-gamegear-mapper-table)

add_executable(time-smoke-gamegear-bios tests/smoke_gamegear_bios.cpp)
target_link_libraries(time-smoke-gamegear-bios time-gamegear-core)
add_test(NAME smoke-gamegear-bios COMMAND time-smoke-gamegear-bios)

add_executable(time-smoke-gamegear-vdp-subsystem tests/smoke_gamegear_vdp_subsystem.cpp)
target_link_libraries(time-smoke-gamegear-vdp-subsystem time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-subsystem COMMAND time-smoke-gamegear-vdp-subsystem)

add_executable(time-smoke-gamegear-vdp-fidelity tests/smoke_gamegear_vdp_fidelity.cpp)
target_link_libraries(time-smoke-gamegear-vdp-fidelity time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-fidelity COMMAND time-smoke-gamegear-vdp-fidelity)

add_executable(time-smoke-gamegear-vdp-hoist tests/smoke_gamegear_vdp_hoist.cpp)
target_link_libraries(time-smoke-gamegear-vdp-hoist time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-hoist COMMAND time-smoke-gamegear-vdp-hoist)

add_executable(time-smoke-gamegear-vdp-tile-flags tests/smoke_gamegear_vdp_tile_flags.cpp)
target_link_libraries(time-smoke-gamegear-vdp-tile-flags time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-tile-flags COMMAND time-smoke-gamegear-vdp-tile-flags)

add_executable(time-smoke-gamegear-vdp-pattern-row tests/smoke_gamegear_vdp_pattern_row.cpp)
target_link_libraries(time-smoke-gamegear-vdp-pattern-row time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-pattern-row COMMAND time-smoke-gamegear-vdp-pattern-row)

add_executable(time-smoke-gamegear-vdp-simple-tile-run tests/smoke_gamegear_vdp_simple_tile_run.cpp)
target_link_libraries(time-smoke-gamegear-vdp-simple-tile-run time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-simple-tile-run COMMAND time-smoke-gamegear-vdp-simple-tile-run)

add_executable(time-perf-gamegear-vdp-bench tests/perf_gamegear_vdp_bench.cpp)
target_link_libraries(time-perf-gamegear-vdp-bench time-gamegear-core)
add_test(NAME perf_gamegear_vdp_bench COMMAND time-perf-gamegear-vdp-bench)
set_tests_properties(perf_gamegear_vdp_bench PROPERTIES TIMEOUT 120)

add_executable(time-perf-concurrency-success-gates tests/perf_concurrency_success_gates.cpp)
target_link_libraries(time-perf-concurrency-success-gates time-gameboy-interpreter-core)
add_test(NAME perf_concurrency_success_gates COMMAND time-perf-concurrency-success-gates)
set_tests_properties(perf_concurrency_success_gates PROPERTIES
    LABELS "performance;concurrency"
    TIMEOUT 120
)

add_executable(time-perf-headless-scheduling-video tests/perf_headless_scheduling_video.cpp)
target_link_libraries(time-perf-headless-scheduling-video time-gameboy-interpreter-core)
add_test(NAME perf_headless_scheduling_video COMMAND time-perf-headless-scheduling-video)
set_tests_properties(perf_headless_scheduling_video PROPERTIES
    LABELS "performance;concurrency"
    TIMEOUT 10
)

add_executable(time-smoke-realtime-video-mailbox tests/smoke_realtime_video_mailbox.cpp)
target_link_libraries(time-smoke-realtime-video-mailbox time-machine-runtime-common)
add_test(NAME smoke-realtime-video-mailbox COMMAND time-smoke-realtime-video-mailbox)

add_executable(time-smoke-gamegear-vdp-cram tests/smoke_gamegear_vdp_cram.cpp)
target_link_libraries(time-smoke-gamegear-vdp-cram time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-cram COMMAND time-smoke-gamegear-vdp-cram)

add_executable(time-smoke-gamegear-vdp-sprites tests/smoke_gamegear_vdp_sprites.cpp)
target_link_libraries(time-smoke-gamegear-vdp-sprites time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-sprites COMMAND time-smoke-gamegear-vdp-sprites)

add_executable(time-smoke-gamegear-vdp-priority tests/smoke_gamegear_vdp_priority.cpp)
target_link_libraries(time-smoke-gamegear-vdp-priority time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-priority COMMAND time-smoke-gamegear-vdp-priority)

add_executable(time-smoke-gamegear-vdp-line-irq tests/smoke_gamegear_vdp_line_irq.cpp)
target_link_libraries(time-smoke-gamegear-vdp-line-irq time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-line-irq COMMAND time-smoke-gamegear-vdp-line-irq)

add_executable(time-smoke-gamegear-vdp-cache tests/smoke_gamegear_vdp_cache.cpp)
target_link_libraries(time-smoke-gamegear-vdp-cache time-gamegear-core)
add_test(NAME smoke-gamegear-vdp-cache COMMAND time-smoke-gamegear-vdp-cache)

add_executable(time-smoke-gamegear-psg-subsystem tests/smoke_gamegear_psg_subsystem.cpp)
target_link_libraries(time-smoke-gamegear-psg-subsystem time-gamegear-core)
add_test(NAME smoke-gamegear-psg-subsystem COMMAND time-smoke-gamegear-psg-subsystem)

add_executable(time-smoke-gamegear-save-persistence tests/smoke_gamegear_save_persistence.cpp)
target_link_libraries(time-smoke-gamegear-save-persistence time-gamegear-core)
add_test(NAME smoke-gamegear-save-persistence COMMAND time-smoke-gamegear-save-persistence)
add_executable(time-smoke-gamegear-save-state tests/smoke_gamegear_save_state.cpp)
target_link_libraries(time-smoke-gamegear-save-state time-gamegear-core)
add_test(NAME smoke-gamegear-save-state COMMAND time-smoke-gamegear-save-state)

add_executable(time-smoke-gamegear-rom-baseline tests/smoke_gamegear_rom_baseline.cpp)
target_link_libraries(time-smoke-gamegear-rom-baseline time-gamegear-core)
add_test(NAME smoke-gamegear-rom-baseline COMMAND time-smoke-gamegear-rom-baseline)

add_executable(time-smoke-snapshot tests/smoke_snapshot.cpp)
add_test(NAME smoke-snapshot COMMAND time-smoke-snapshot)
add_executable(time-smoke-register-snapshot tests/smoke_register_snapshot.cpp)
add_test(NAME smoke-register-snapshot COMMAND time-smoke-register-snapshot)
add_executable(time-smoke-register-descriptors tests/smoke_register_descriptors.cpp)
add_test(NAME smoke-register-descriptors COMMAND time-smoke-register-descriptors)
add_executable(time-smoke-instruction-cycle tests/smoke_instruction_cycle.cpp)
target_link_libraries(time-smoke-instruction-cycle time-gameboy-interpreter-core)
add_test(NAME smoke-instruction-cycle COMMAND time-smoke-instruction-cycle)
add_executable(time-smoke-executor tests/smoke_executor.cpp)
target_link_libraries(time-smoke-executor time-gameboy-interpreter-core)
add_test(NAME smoke-executor COMMAND time-smoke-executor)
add_executable(time-smoke-plugin-executor tests/smoke_plugin_executor.cpp)
target_link_libraries(time-smoke-plugin-executor time-gameboy-interpreter-core)
add_test(NAME smoke-plugin-executor COMMAND time-smoke-plugin-executor)
add_executable(time-smoke-plugin-abi tests/smoke_plugin_abi.cpp)
target_link_libraries(time-smoke-plugin-abi time-gameboy-interpreter-core)
add_test(NAME smoke-plugin-abi COMMAND time-smoke-plugin-abi)
add_library(time-test-c-executor-plugin MODULE tests/fixtures/test_executor_plugin.c)
set_target_properties(time-test-c-executor-plugin PROPERTIES PREFIX "")
add_library(time-test-missing-plugin-entrypoint MODULE
    tests/fixtures/test_missing_plugin_entrypoint.c)
set_target_properties(time-test-missing-plugin-entrypoint PROPERTIES PREFIX "")
add_library(time-test-malformed-executor-plugin MODULE
    tests/fixtures/test_malformed_executor_plugin.c)
set_target_properties(time-test-malformed-executor-plugin PROPERTIES PREFIX "")
add_executable(time-smoke-dynamic-plugin-module tests/smoke_dynamic_plugin_module.cpp)
target_link_libraries(time-smoke-dynamic-plugin-module
    time-gameboy-interpreter-core time-gamegear-core time-dynamic-plugin-loader)
add_test(NAME smoke-dynamic-plugin-module
    COMMAND time-smoke-dynamic-plugin-module
        $<TARGET_FILE:time-test-c-executor-plugin>
        $<TARGET_FILE:time-test-missing-plugin-entrypoint>
        $<TARGET_FILE:time-test-malformed-executor-plugin>)
add_library(time-test-c-frontend-plugin MODULE tests/fixtures/test_frontend_plugin.c)
target_include_directories(time-test-c-frontend-plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(time-test-c-frontend-plugin PROPERTIES PREFIX "")
add_executable(time-smoke-dynamic-frontend-module tests/smoke_dynamic_frontend_module.cpp)
target_link_libraries(time-smoke-dynamic-frontend-module
    time-gameboy-interpreter-core time-dynamic-plugin-loader)
add_test(NAME smoke-dynamic-frontend-module
    COMMAND time-smoke-dynamic-frontend-module $<TARGET_FILE:time-test-c-frontend-plugin>)
add_library(time-test-c-audio-output-plugin MODULE tests/fixtures/test_audio_output_plugin.c)
target_include_directories(time-test-c-audio-output-plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(time-test-c-audio-output-plugin PROPERTIES PREFIX "")
add_library(time-test-malformed-audio-output-plugin MODULE
    tests/fixtures/test_malformed_audio_output_plugin.c)
target_include_directories(time-test-malformed-audio-output-plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(time-test-malformed-audio-output-plugin PROPERTIES PREFIX "")
add_executable(time-smoke-dynamic-audio-output-module
    tests/smoke_dynamic_audio_output_module.cpp)
target_link_libraries(time-smoke-dynamic-audio-output-module
    time-gameboy-interpreter-core time-audio-output-loader)
add_test(NAME smoke-dynamic-audio-output-module
    COMMAND time-smoke-dynamic-audio-output-module
        $<TARGET_FILE:time-test-c-audio-output-plugin>
        $<TARGET_FILE:time-test-malformed-audio-output-plugin>)
if (TARGET time-glfw-frontend-plugin)
    add_executable(time-smoke-glfw-frontend-plugin tests/smoke_glfw_frontend_plugin.cpp)
    target_link_libraries(time-smoke-glfw-frontend-plugin time-frontend-loader)
    add_test(NAME smoke-glfw-frontend-plugin
        COMMAND time-smoke-glfw-frontend-plugin $<TARGET_FILE:time-glfw-frontend-plugin>)
endif()
add_executable(time-smoke-plugin-io tests/smoke_plugin_io.cpp)
target_link_libraries(time-smoke-plugin-io time-gameboy-interpreter-core)
add_test(NAME smoke-plugin-io COMMAND time-smoke-plugin-io)
if (TARGET time-sdl-frontend-plugin)
    add_executable(time-smoke-sdl-frontend-plugin-api tests/smoke_sdl_frontend_plugin_api.cpp)
    target_link_libraries(time-smoke-sdl-frontend-plugin-api time-frontend-loader)
    add_test(NAME smoke-sdl-frontend-plugin-api COMMAND time-smoke-sdl-frontend-plugin-api)
    add_executable(time-smoke-sdl-frontend-plugin tests/smoke_sdl_frontend_plugin.cpp)
    target_link_libraries(time-smoke-sdl-frontend-plugin time-gameboy-interpreter-core time-frontend-loader time-simd-pixel-ops)
    add_test(NAME smoke-sdl-frontend-plugin COMMAND time-smoke-sdl-frontend-plugin)
    add_executable(time-smoke-sdl-frontend-gamegear tests/smoke_sdl_frontend_gamegear.cpp)
    target_link_libraries(time-smoke-sdl-frontend-gamegear time-gamegear-core time-frontend-loader)
    add_test(NAME smoke-sdl-frontend-gamegear COMMAND time-smoke-sdl-frontend-gamegear)
endif()
add_executable(time-smoke-audio-resampler tests/smoke_audio_resampler.cpp)
target_link_libraries(time-smoke-audio-resampler time-gameboy-interpreter-core)
add_test(NAME smoke-audio-resampler COMMAND time-smoke-audio-resampler)
add_executable(time-smoke-audio-engine tests/smoke_audio_engine.cpp)
target_link_libraries(time-smoke-audio-engine time-gameboy-interpreter-core)
add_test(NAME smoke-audio-engine COMMAND time-smoke-audio-engine)
add_executable(time-smoke-audio-kpi-status tests/smoke_audio_kpi_status.cpp)
add_test(NAME smoke-audio-kpi-status COMMAND time-smoke-audio-kpi-status)
add_executable(time-smoke-input-engine tests/smoke_input_engine.cpp)
add_test(NAME smoke-input-engine COMMAND time-smoke-input-engine)
add_executable(time-smoke-input-service tests/smoke_input_service.cpp)
add_test(NAME smoke-input-service COMMAND time-smoke-input-service)
if (TARGET time-sdl-frontend-plugin)
    add_executable(time-smoke-input-transport tests/smoke_input_transport.cpp)
    target_link_libraries(time-smoke-input-transport time-gameboy-interpreter-core time-frontend-loader)
    add_test(NAME smoke-input-transport COMMAND time-smoke-input-transport)
endif()
add_executable(time-smoke-input-fallback tests/smoke_input_fallback.cpp)
add_test(NAME smoke-input-fallback COMMAND time-smoke-input-fallback)
add_executable(time-smoke-emulator-config tests/smoke_emulator_config.cpp)
target_link_libraries(time-smoke-emulator-config time-emulator-config)
add_test(NAME smoke-emulator-config COMMAND time-smoke-emulator-config)
add_executable(time-smoke-diagnostics-json tests/smoke_diagnostics_json.cpp)
target_link_libraries(time-smoke-diagnostics-json time-machine-runtime-common)
add_test(NAME smoke-diagnostics-json COMMAND time-smoke-diagnostics-json)
add_executable(time-smoke-machine-factory tests/smoke_machine_factory.cpp)
target_link_libraries(time-smoke-machine-factory time-machine-factory)
add_test(NAME smoke-machine-factory COMMAND time-smoke-machine-factory)
add_executable(time-smoke-emulator-host tests/smoke_emulator_host.cpp)
target_link_libraries(time-smoke-emulator-host time-emulator-host)
add_test(NAME smoke-emulator-host COMMAND time-smoke-emulator-host)
add_executable(time-smoke-video-engine tests/smoke_video_engine.cpp)
target_link_libraries(time-smoke-video-engine time-gameboy-interpreter-core)
add_test(NAME smoke-video-engine COMMAND time-smoke-video-engine)
add_executable(time-smoke-video-engine-mailbox tests/smoke_video_engine_mailbox.cpp)
target_link_libraries(time-smoke-video-engine-mailbox time-gameboy-interpreter-core)
add_test(NAME smoke-video-engine-mailbox COMMAND time-smoke-video-engine-mailbox)
add_executable(time-smoke-video-service tests/smoke_video_service.cpp)
target_link_libraries(time-smoke-video-service time-gameboy-interpreter-core)
add_test(NAME smoke-video-service COMMAND time-smoke-video-service)
add_executable(time-smoke-video-transport tests/smoke_video_transport.cpp)
target_link_libraries(time-smoke-video-transport time-gameboy-interpreter-core time-headless-video-adapter)
add_test(NAME smoke-video-transport COMMAND time-smoke-video-transport)
add_executable(time-smoke-video-fallback tests/smoke_video_fallback.cpp)
target_link_libraries(time-smoke-video-fallback time-gameboy-interpreter-core)
add_test(NAME smoke-video-fallback COMMAND time-smoke-video-fallback)
add_executable(time-smoke-visual-override tests/smoke_visual_override.cpp)
target_link_libraries(time-smoke-visual-override time-gameboy-interpreter-core)
add_test(NAME smoke-visual-override COMMAND time-smoke-visual-override)
add_executable(time-smoke-visual-override-async-polling tests/smoke_visual_override_async_polling.cpp)
target_link_libraries(time-smoke-visual-override-async-polling time-gameboy-interpreter-core)
add_test(NAME smoke-visual-override-async-polling COMMAND time-smoke-visual-override-async-polling)
add_executable(time-smoke-visual-manifest-tests tests/visual_manifest_tests.cpp)
target_link_libraries(time-smoke-visual-manifest-tests time-gameboy-interpreter-core)
add_test(NAME visual_manifest_tests COMMAND time-smoke-visual-manifest-tests)
add_executable(time-smoke-visual-capture-tests tests/visual_capture_tests.cpp)
target_link_libraries(time-smoke-visual-capture-tests time-gameboy-interpreter-core)
add_test(NAME visual_capture_tests COMMAND time-smoke-visual-capture-tests)
add_executable(time-smoke-texture-pack-docs tests/smoke_texture_pack_docs.cpp)
target_link_libraries(time-smoke-texture-pack-docs time-gameboy-interpreter-core)
target_compile_definitions(time-smoke-texture-pack-docs PRIVATE PROTO_TIME_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME smoke_texture_pack_docs COMMAND time-smoke-texture-pack-docs)
add_executable(time-smoke-audio-service tests/smoke_audio_service.cpp)
target_link_libraries(time-smoke-audio-service time-gameboy-interpreter-core)
add_test(NAME smoke-audio-service COMMAND time-smoke-audio-service)
add_executable(time-smoke-lifecycle-epoch-barrier tests/smoke_lifecycle_epoch_barrier.cpp)
target_link_libraries(time-smoke-lifecycle-epoch-barrier time-gameboy-interpreter-core)
add_test(NAME smoke-lifecycle-epoch-barrier COMMAND time-smoke-lifecycle-epoch-barrier)
add_executable(time-smoke-lifecycle-coordinator tests/smoke_lifecycle_coordinator.cpp)
target_link_libraries(time-smoke-lifecycle-coordinator time-gameboy-interpreter-core)
add_test(NAME smoke-lifecycle-coordinator COMMAND time-smoke-lifecycle-coordinator)
add_executable(time-smoke-lifecycle-contract tests/smoke_lifecycle_contract.cpp)
target_link_libraries(time-smoke-lifecycle-contract time-gameboy-interpreter-core)
add_test(NAME smoke-lifecycle-contract COMMAND time-smoke-lifecycle-contract)
add_executable(time-smoke-audio-pipeline tests/smoke_audio_pipeline.cpp)
target_link_libraries(time-smoke-audio-pipeline time-gameboy-interpreter-core)
add_test(NAME smoke-audio-pipeline COMMAND time-smoke-audio-pipeline)
if (TARGET time-sdl-audio-output-plugin)
    add_executable(time-smoke-sdl-audio-output-module tests/smoke_sdl_audio_output_backend.cpp)
    target_link_libraries(time-smoke-sdl-audio-output-module
        time-gameboy-interpreter-core time-audio-output-loader)
    add_test(NAME smoke-sdl-audio-output-module COMMAND time-smoke-sdl-audio-output-module
        $<TARGET_FILE:time-sdl-audio-output-plugin>)
endif()
add_executable(time-smoke-dummy-audio-output tests/smoke_dummy_audio_output.cpp)
target_link_libraries(time-smoke-dummy-audio-output time-gameboy-interpreter-core time-dummy-audio-output)
add_test(NAME smoke-dummy-audio-output COMMAND time-smoke-dummy-audio-output)
add_executable(time-smoke-file-audio-output tests/smoke_file_audio_output.cpp)
target_link_libraries(time-smoke-file-audio-output time-gameboy-interpreter-core time-file-audio-output)
add_test(NAME smoke-file-audio-output COMMAND time-smoke-file-audio-output)
add_executable(time-smoke-apu-audio tests/smoke_apu_audio.cpp)
target_link_libraries(time-smoke-apu-audio time-gameboy-interpreter-core)
add_test(NAME smoke-apu-audio COMMAND time-smoke-apu-audio)
add_executable(time-smoke-machine-boot tests/smoke_machine_boot.cpp)
target_link_libraries(time-smoke-machine-boot time-gameboy-interpreter-core)
add_test(NAME smoke-machine-boot COMMAND time-smoke-machine-boot)
add_executable(time-smoke-gameboy-cartridge-save tests/smoke_gameboy_cartridge_save.cpp)
target_link_libraries(time-smoke-gameboy-cartridge-save time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-cartridge-save COMMAND time-smoke-gameboy-cartridge-save)
add_executable(time-smoke-gameboy-save-state tests/smoke_gameboy_save_state.cpp)
target_link_libraries(time-smoke-gameboy-save-state time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-save-state COMMAND time-smoke-gameboy-save-state)
add_executable(time-smoke-gameboy-save-state-staged tests/smoke_gameboy_save_state_staged.cpp)
target_link_libraries(time-smoke-gameboy-save-state-staged time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-save-state-staged COMMAND time-smoke-gameboy-save-state-staged)
add_executable(time-smoke-gameboy-mapper tests/smoke_gameboy_mapper.cpp)
target_link_libraries(time-smoke-gameboy-mapper time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-mapper COMMAND time-smoke-gameboy-mapper)
add_executable(time-smoke-gameboy-ppu tests/smoke_gameboy_ppu.cpp)
target_link_libraries(time-smoke-gameboy-ppu time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-ppu COMMAND time-smoke-gameboy-ppu)
add_executable(time-smoke-background-task-service tests/smoke_background_task_service.cpp)
target_link_libraries(time-smoke-background-task-service time-machine-runtime-common)
add_test(NAME smoke-background-task-service COMMAND time-smoke-background-task-service)

# Background thread pool smoke test (directly exercises the pool).
add_executable(time-smoke-background-thread-pool tests/smoke_background_thread_pool.cpp)
target_link_libraries(time-smoke-background-thread-pool time-machine-runtime-common)
add_test(NAME smoke-background-thread-pool COMMAND time-smoke-background-thread-pool)
add_executable(time-smoke-image-decode-pipeline tests/smoke_image_decode_pipeline.cpp)
target_link_libraries(time-smoke-image-decode-pipeline time-gameboy-interpreter-core)
add_test(NAME smoke-image-decode-pipeline COMMAND time-smoke-image-decode-pipeline)
add_executable(time-smoke-visual-override-decode-integration tests/smoke_visual_override_decode_integration.cpp)
target_link_libraries(time-smoke-visual-override-decode-integration time-gameboy-interpreter-core)
add_test(NAME smoke-visual-override-decode-integration COMMAND time-smoke-visual-override-decode-integration)
add_executable(time-smoke-debug-snapshot-service tests/smoke_debug_snapshot_service.cpp)
target_link_libraries(time-smoke-debug-snapshot-service time-machine-runtime-common)
add_test(NAME smoke-debug-snapshot-service COMMAND time-smoke-debug-snapshot-service)
if (TARGET time-sdl-frontend-plugin)
    add_executable(time-smoke-debug-snapshot-sdl-plugin tests/smoke_debug_snapshot_sdl_plugin.cpp)
    target_link_libraries(time-smoke-debug-snapshot-sdl-plugin time-gamegear-core time-frontend-loader)
    add_test(NAME smoke-debug-snapshot-sdl-plugin COMMAND time-smoke-debug-snapshot-sdl-plugin)
endif()

if (UNIX AND NOT APPLE)
    add_test(NAME smoke-time-emulator-no-sdl-dependency
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:timeEmulator>
            -P ${CMAKE_SOURCE_DIR}/tests/check_no_sdl_dependency.cmake)
endif()

install(TARGETS timeEmulator RUNTIME DESTINATION bin)
if (TARGET time-sdl-frontend-plugin)
    install(TARGETS time-sdl-frontend-plugin LIBRARY DESTINATION bin)
endif()
if (TARGET time-sdl-audio-output-plugin)
    install(TARGETS time-sdl-audio-output-plugin LIBRARY DESTINATION bin)
endif()
if (TARGET time-glfw-frontend-plugin)
    install(TARGETS time-glfw-frontend-plugin LIBRARY DESTINATION bin)
endif()
add_executable(time-smoke-memory-map tests/smoke_memory_map.cpp)
target_link_libraries(time-smoke-memory-map time-gameboy-interpreter-core)
add_test(NAME smoke-memory-map COMMAND time-smoke-memory-map)
add_executable(time-smoke-runtime-context tests/smoke_runtime_context.cpp)
target_link_libraries(time-smoke-runtime-context time-gameboy-interpreter-core)
add_test(NAME smoke-runtime-context COMMAND time-smoke-runtime-context)
add_executable(time-smoke-opcode-table tests/smoke_opcode_table.cpp)
target_link_libraries(time-smoke-opcode-table time-gameboy-interpreter-core)
add_test(NAME smoke-opcode-table COMMAND time-smoke-opcode-table)
add_executable(time-smoke-interpreter-math-i8 tests/smoke_interpreter_math_i8.cpp)
target_link_libraries(time-smoke-interpreter-math-i8 time-gameboy-interpreter-core)
add_test(NAME smoke-interpreter-math-i8 COMMAND time-smoke-interpreter-math-i8)
add_executable(time-smoke-trace-executor tests/smoke_trace_executor.cpp)
target_link_libraries(time-smoke-trace-executor time-gameboy-interpreter-core)
add_test(NAME smoke-trace-executor COMMAND time-smoke-trace-executor)

add_executable(time-smoke-simd-pixel-ops tests/smoke_simd_pixel_ops.cpp)
target_link_libraries(time-smoke-simd-pixel-ops time-simd-pixel-ops)
add_test(NAME smoke-simd-pixel-ops COMMAND time-smoke-simd-pixel-ops)
add_executable(time-smoke-simd-pixel-ops-scalar tests/smoke_simd_pixel_ops.cpp)
target_compile_definitions(time-smoke-simd-pixel-ops-scalar PRIVATE BMMQ_EXPECT_SCALAR=1)
target_link_libraries(time-smoke-simd-pixel-ops-scalar time-simd-pixel-ops-scalar)
add_test(NAME smoke-simd-pixel-ops-scalar COMMAND time-smoke-simd-pixel-ops-scalar)

add_executable(time-perf-simd-pixel-ops tests/perf_simd_pixel_ops.cpp)
target_link_libraries(time-perf-simd-pixel-ops time-simd-pixel-ops)
add_test(NAME perf-simd-pixel-ops COMMAND time-perf-simd-pixel-ops)
set_tests_properties(perf-simd-pixel-ops PROPERTIES LABELS performance)

# Timing engine smoke test
add_executable(time-smoke-timing-engine tests/smoke_timing_engine.cpp)
target_link_libraries(time-smoke-timing-engine time-gameboy-interpreter-core)
add_test(NAME smoke-timing-engine COMMAND time-smoke-timing-engine)

add_executable(time-smoke-timing-service tests/smoke_timing_service.cpp)
target_link_libraries(time-smoke-timing-service time-gameboy-interpreter-core)
add_test(NAME smoke-timing-service COMMAND time-smoke-timing-service)

# Slim packets smoke test
add_executable(time-smoke-slim-packets tests/smoke_slim_packets.cpp)
add_executable(time-smoke-block-cache tests/smoke_block_cache.cpp)
target_link_libraries(time-smoke-block-cache time-gameboy-interpreter-core)
target_link_libraries(time-smoke-slim-packets time-gameboy-interpreter-core)
add_test(NAME smoke-slim-packets COMMAND time-smoke-slim-packets)
add_test(NAME smoke-block-cache COMMAND time-smoke-block-cache)

add_executable(time-smoke-execution-slice tests/smoke_execution_slice.cpp)
target_link_libraries(time-smoke-execution-slice time-gameboy-interpreter-core)
add_test(NAME smoke-execution-slice COMMAND time-smoke-execution-slice)

add_executable(time-smoke-intermediate-representation tests/smoke_intermediate_representation.cpp)
add_test(NAME smoke-intermediate-representation COMMAND time-smoke-intermediate-representation)

add_executable(time-smoke-gameboy-portable-ir tests/smoke_gameboy_portable_ir.cpp)
target_link_libraries(time-smoke-gameboy-portable-ir time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-portable-ir COMMAND time-smoke-gameboy-portable-ir)

add_executable(time-smoke-gameboy-ir-opcode-differential
    tests/smoke_gameboy_ir_opcode_differential.cpp)
target_link_libraries(time-smoke-gameboy-ir-opcode-differential
    time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-ir-opcode-differential
    COMMAND time-smoke-gameboy-ir-opcode-differential)

add_executable(time-smoke-gameboy-ir-boundary-corpus
    tests/smoke_gameboy_ir_boundary_corpus.cpp)
target_link_libraries(time-smoke-gameboy-ir-boundary-corpus
    time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-ir-boundary-corpus
    COMMAND time-smoke-gameboy-ir-boundary-corpus)

add_executable(time-smoke-gameboy-ir-block-backend
    tests/smoke_gameboy_ir_block_backend.cpp)
target_link_libraries(time-smoke-gameboy-ir-block-backend
    time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-ir-block-backend
    COMMAND time-smoke-gameboy-ir-block-backend)

add_executable(time-smoke-gameboy-native-ir
    tests/smoke_gameboy_native_ir.cpp)
target_link_libraries(time-smoke-gameboy-native-ir
    time-gameboy-interpreter-core)
add_test(NAME smoke-gameboy-native-ir COMMAND time-smoke-gameboy-native-ir)

add_executable(time-perf-gameboy-block-cache tests/perf_gameboy_block_cache.cpp)
target_link_libraries(time-perf-gameboy-block-cache time-gameboy-interpreter-core)
add_test(NAME perf-gameboy-block-cache COMMAND time-perf-gameboy-block-cache)
set_tests_properties(perf-gameboy-block-cache PROPERTIES
    LABELS "performance;concurrency"
    TIMEOUT 120
)

# Save-state smoke tests for Game Boy memory map, APU, and input.
add_executable(time-smoke-memory-map-state tests/smoke_memory_map_state.cpp)
target_link_libraries(time-smoke-memory-map-state time-gameboy-interpreter-core)
add_test(NAME smoke-memory-map-state COMMAND time-smoke-memory-map-state)
add_executable(time-smoke-apu-state-roundtrip tests/smoke_apu_state_roundtrip.cpp)
target_link_libraries(time-smoke-apu-state-roundtrip time-gameboy-interpreter-core)
add_test(NAME smoke-apu-state-roundtrip COMMAND time-smoke-apu-state-roundtrip)
add_executable(time-smoke-input-state-roundtrip tests/smoke_input_state_roundtrip.cpp)
target_link_libraries(time-smoke-input-state-roundtrip time-gameboy-interpreter-core)
add_test(NAME smoke-input-state-roundtrip COMMAND time-smoke-input-state-roundtrip)

add_executable(time-smoke-dma-tests tests/dma_tests.cpp)
target_link_libraries(time-smoke-dma-tests time-gameboy-interpreter-core)
add_test(NAME dma-completion-notification COMMAND time-smoke-dma-tests)

add_executable(time-smoke-vram-tests tests/vram_tests.cpp)
target_link_libraries(time-smoke-vram-tests time-gameboy-interpreter-core)
add_test(NAME auto-bank-switch-on-sprite-access COMMAND time-smoke-vram-tests)

# The test suite uses C assertions extensively. Keep those checks active in
# release-style configurations, including every performance gate.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    get_property(BMMQ_ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(bmmq_target IN LISTS BMMQ_ALL_TARGETS)
        if (bmmq_target MATCHES "^time-(smoke|perf)-")
            target_compile_options(${bmmq_target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
