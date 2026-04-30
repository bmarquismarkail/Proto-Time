cmake_minimum_required(VERSION 3.10)

project(timeEmulator)

option(BMMQ_ENABLE_TSAN "Enable ThreadSanitizer for race detection builds" OFF)

find_package(SDL2 QUIET)
find_package(ZLIB REQUIRED)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

set (CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set (CMAKE_CXX_STANDARD_REQUIRED True)
include_directories(. templ memory cores)
add_compile_options(-Wall -Wextra )

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
    machine/MachineLifecycleCoordinator.cpp
    machine/TimingService.cpp
    machine/VisualCaptureWriter.cpp
    machine/VisualOverrideService.cpp
    machine/VisualPackManifest.cpp
)
set_target_properties(time-machine-runtime-common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(time-machine-runtime-common PUBLIC ZLIB::ZLIB)

add_library(time-gameboy-interpreter-core
	cores/gameboy/decode/gb_decoder.cpp
	cores/gameboy/decode/gb_interpreter.cpp
    cores/gameboy/gameboy.cpp
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

set_target_properties(time-gameboy-interpreter-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(time-gameboy-interpreter-core PUBLIC time-machine-runtime-common)
add_library(time-sdl-frontend-loader
    machine/plugins/SdlFrontendPluginLoader.cpp
)
target_link_libraries(time-sdl-frontend-loader ${CMAKE_DL_LIBS})
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
target_link_libraries(time-emulator-config PUBLIC time-machine-factory)
add_library(time-emulator-host STATIC
    emulator/EmulatorHost.cpp
)
target_link_libraries(time-emulator-host
    PUBLIC
        time-machine-factory
        time-emulator-config
)
set(BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE 0)
add_library(time-sdl-audio-output STATIC
    machine/plugins/sdl_frontend/SdlAudioOutput.cpp
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
add_library(time-sdl-video-adapter STATIC
    machine/plugins/video/adapters/SdlVideoPresenter.cpp
)
set_target_properties(time-sdl-audio-output PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(time-dummy-audio-output PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(time-file-audio-output PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(time-headless-video-adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(time-sdl-video-adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(time-sdl-frontend-plugin SHARED
    machine/plugins/sdl_frontend/SdlFrontendPlugin.cpp
)
target_link_libraries(time-sdl-frontend-plugin PRIVATE time-gameboy-interpreter-core)
add_executable(timeEmulator emulator.cpp)
target_link_libraries(timeEmulator time-emulator-host time-sdl-frontend-loader)
if (TARGET SDL2::SDL2)
    target_link_libraries(time-sdl-audio-output PRIVATE SDL2::SDL2)
    target_link_libraries(time-sdl-video-adapter PRIVATE SDL2::SDL2)
    target_link_libraries(time-sdl-frontend-plugin PRIVATE SDL2::SDL2)
    set(BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE 1)
elseif (SDL2_FOUND)
    target_include_directories(time-sdl-audio-output PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(time-sdl-audio-output PRIVATE ${SDL2_LIBRARIES})
    target_include_directories(time-sdl-video-adapter PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(time-sdl-video-adapter PRIVATE ${SDL2_LIBRARIES})
    target_include_directories(time-sdl-frontend-plugin PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(time-sdl-frontend-plugin PRIVATE ${SDL2_LIBRARIES})
    set(BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE 1)
endif()
target_compile_definitions(time-sdl-audio-output PRIVATE BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL=${BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE})
target_compile_definitions(time-sdl-video-adapter PUBLIC BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL=${BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE})
target_compile_definitions(time-sdl-frontend-plugin PRIVATE BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL=${BMMQ_SDL_FRONTEND_COMPILED_WITH_SDL_VALUE})
target_link_libraries(time-sdl-frontend-plugin PRIVATE time-sdl-audio-output time-dummy-audio-output time-file-audio-output time-sdl-video-adapter)

enable_testing()
add_executable(time-smoke-gamegear-core tests/smoke_gamegear_core.cpp)
target_link_libraries(time-smoke-gamegear-core time-gamegear-core)
add_test(NAME smoke_gamegear_core COMMAND time-smoke-gamegear-core)
add_executable(time-smoke-gamegear-cartridge
    tests/smoke_gamegear_cartridge.cpp
    cores/gamegear/GameGearCartridge.cpp
)
add_test(NAME smoke_gamegear_cartridge COMMAND time-smoke-gamegear-cartridge)
add_executable(time-smoke-gamegear-banking tests/smoke_gamegear_banking.cpp)
target_link_libraries(time-smoke-gamegear-banking time-gamegear-core)
add_test(NAME smoke_gamegear_banking COMMAND time-smoke-gamegear-banking)
add_executable(time-smoke-gamegear-machine-banking tests/smoke_gamegear_machine_banking.cpp)
target_link_libraries(time-smoke-gamegear-machine-banking time-gamegear-core)
add_test(NAME smoke_gamegear_machine_banking COMMAND time-smoke-gamegear-machine-banking)

add_executable(time-smoke-gamegear-vblank-irq tests/smoke_gamegear_vblank_irq.cpp)
target_link_libraries(time-smoke-gamegear-vblank-irq time-gamegear-core)
add_test(NAME smoke_gamegear_vblank_irq COMMAND time-smoke-gamegear-vblank-irq)

add_executable(time-probe-gamegear-wait-loop EXCLUDE_FROM_ALL tests/probe_gamegear_wait_loop.cpp)
target_link_libraries(time-probe-gamegear-wait-loop time-gamegear-core)

add_executable(time-smoke-ei-reti tests/smoke_ei_reti.cpp)
target_link_libraries(time-smoke-ei-reti time-gamegear-core)
add_test(NAME smoke_ei_reti COMMAND time-smoke-ei-reti)

add_executable(time-smoke-gamegear-z80-baseline tests/smoke_gamegear_z80_baseline.cpp)
target_link_libraries(time-smoke-gamegear-z80-baseline time-gamegear-core)
add_test(NAME smoke_gamegear_z80_baseline COMMAND time-smoke-gamegear-z80-baseline)

add_executable(time-smoke-gamegear-z80-extended tests/smoke_gamegear_z80_extended.cpp)
target_link_libraries(time-smoke-gamegear-z80-extended time-gamegear-core)
add_test(NAME smoke_gamegear_z80_extended COMMAND time-smoke-gamegear-z80-extended)

add_executable(time-smoke-gamegear-z80-halt tests/smoke_gamegear_z80_halt.cpp)
target_link_libraries(time-smoke-gamegear-z80-halt time-gamegear-core)
add_test(NAME smoke_gamegear_z80_halt COMMAND time-smoke-gamegear-z80-halt)

add_executable(time-smoke-gamegear-z80-interrupts tests/smoke_gamegear_z80_interrupts.cpp)
target_link_libraries(time-smoke-gamegear-z80-interrupts time-gamegear-core)
add_test(NAME smoke_gamegear_z80_interrupts COMMAND time-smoke-gamegear-z80-interrupts)

# Focused FD-indexed behavior tests
add_executable(time-smoke-gamegear-z80-fd-indexed tests/smoke_gamegear_z80_fd_indexed.cpp)
target_link_libraries(time-smoke-gamegear-z80-fd-indexed time-gamegear-core)
add_test(NAME smoke_gamegear_z80_fd_indexed COMMAND time-smoke-gamegear-z80-fd-indexed)

add_executable(time-smoke-gamegear-system-ports tests/smoke_gamegear_system_ports.cpp)
target_link_libraries(time-smoke-gamegear-system-ports time-gamegear-core)
add_test(NAME smoke_gamegear_system_ports COMMAND time-smoke-gamegear-system-ports)

add_executable(time-smoke-gamegear-audio tests/smoke_gamegear_audio.cpp)
target_link_libraries(time-smoke-gamegear-audio time-gamegear-core)
add_test(NAME smoke_gamegear_audio COMMAND time-smoke-gamegear-audio)

add_executable(time-smoke-gamegear-mapper-subsystem tests/smoke_gamegear_mapper_subsystem.cpp)
target_link_libraries(time-smoke-gamegear-mapper-subsystem time-gamegear-core)
add_test(NAME smoke_gamegear_mapper_subsystem COMMAND time-smoke-gamegear-mapper-subsystem)

add_executable(time-smoke-gamegear-3155235-reset tests/smoke_gamegear_3155235_reset.cpp)
target_link_libraries(time-smoke-gamegear-3155235-reset time-gamegear-core)
add_test(NAME smoke_gamegear_3155235_reset COMMAND time-smoke-gamegear-3155235-reset)

add_executable(time-smoke-gamegear-mapper-table tests/smoke_gamegear_mapper_table.cpp)
target_link_libraries(time-smoke-gamegear-mapper-table time-gamegear-core)
add_test(NAME smoke_gamegear_mapper_table COMMAND time-smoke-gamegear-mapper-table)

add_executable(time-smoke-gamegear-bios tests/smoke_gamegear_bios.cpp)
target_link_libraries(time-smoke-gamegear-bios time-gamegear-core)
add_test(NAME smoke_gamegear_bios COMMAND time-smoke-gamegear-bios)

add_executable(time-smoke-gamegear-vdp-subsystem tests/smoke_gamegear_vdp_subsystem.cpp)
target_link_libraries(time-smoke-gamegear-vdp-subsystem time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_subsystem COMMAND time-smoke-gamegear-vdp-subsystem)

add_executable(time-smoke-gamegear-vdp-fidelity tests/smoke_gamegear_vdp_fidelity.cpp)
target_link_libraries(time-smoke-gamegear-vdp-fidelity time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_fidelity COMMAND time-smoke-gamegear-vdp-fidelity)

add_executable(time-smoke-gamegear-vdp-hoist tests/smoke_gamegear_vdp_hoist.cpp)
target_link_libraries(time-smoke-gamegear-vdp-hoist time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_hoist COMMAND time-smoke-gamegear-vdp-hoist)

add_executable(time-smoke-gamegear-vdp-tile-flags tests/smoke_gamegear_vdp_tile_flags.cpp)
target_link_libraries(time-smoke-gamegear-vdp-tile-flags time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_tile_flags COMMAND time-smoke-gamegear-vdp-tile-flags)

add_executable(time-smoke-gamegear-vdp-pattern-row tests/smoke_gamegear_vdp_pattern_row.cpp)
target_link_libraries(time-smoke-gamegear-vdp-pattern-row time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_pattern_row COMMAND time-smoke-gamegear-vdp-pattern-row)

add_executable(time-perf-gamegear-vdp-bench tests/perf_gamegear_vdp_bench.cpp)
target_link_libraries(time-perf-gamegear-vdp-bench time-gamegear-core)
add_test(NAME perf_gamegear_vdp_bench COMMAND time-perf-gamegear-vdp-bench)

add_executable(time-smoke-gamegear-vdp-cram tests/smoke_gamegear_vdp_cram.cpp)
target_link_libraries(time-smoke-gamegear-vdp-cram time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_cram COMMAND time-smoke-gamegear-vdp-cram)

add_executable(time-smoke-gamegear-vdp-sprites tests/smoke_gamegear_vdp_sprites.cpp)
target_link_libraries(time-smoke-gamegear-vdp-sprites time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_sprites COMMAND time-smoke-gamegear-vdp-sprites)

add_executable(time-smoke-gamegear-vdp-priority tests/smoke_gamegear_vdp_priority.cpp)
target_link_libraries(time-smoke-gamegear-vdp-priority time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_priority COMMAND time-smoke-gamegear-vdp-priority)

add_executable(time-smoke-gamegear-vdp-line-irq tests/smoke_gamegear_vdp_line_irq.cpp)
target_link_libraries(time-smoke-gamegear-vdp-line-irq time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_line_irq COMMAND time-smoke-gamegear-vdp-line-irq)

add_executable(time-smoke-gamegear-vdp-cache tests/smoke_gamegear_vdp_cache.cpp)
target_link_libraries(time-smoke-gamegear-vdp-cache time-gamegear-core)
add_test(NAME smoke_gamegear_vdp_cache COMMAND time-smoke-gamegear-vdp-cache)

add_executable(time-smoke-gamegear-psg-subsystem tests/smoke_gamegear_psg_subsystem.cpp)
target_link_libraries(time-smoke-gamegear-psg-subsystem time-gamegear-core)
add_test(NAME smoke_gamegear_psg_subsystem COMMAND time-smoke-gamegear-psg-subsystem)

add_executable(time-smoke-gamegear-save-persistence tests/smoke_gamegear_save_persistence.cpp)
target_link_libraries(time-smoke-gamegear-save-persistence time-gamegear-core)
add_test(NAME smoke_gamegear_save_persistence COMMAND time-smoke-gamegear-save-persistence)

add_executable(time-smoke-gamegear-rom-baseline tests/smoke_gamegear_rom_baseline.cpp)
target_link_libraries(time-smoke-gamegear-rom-baseline time-gamegear-core)
add_test(NAME smoke_gamegear_rom_baseline COMMAND time-smoke-gamegear-rom-baseline)

add_executable(time-smoke-snapshot tests/smoke_snapshot.cpp)
add_test(NAME smoke_snapshot COMMAND time-smoke-snapshot)
add_executable(time-smoke-register-snapshot tests/smoke_register_snapshot.cpp)
add_test(NAME smoke_register_snapshot COMMAND time-smoke-register-snapshot)
add_executable(time-smoke-register-descriptors tests/smoke_register_descriptors.cpp)
add_test(NAME smoke_register_descriptors COMMAND time-smoke-register-descriptors)
add_executable(time-smoke-instruction-cycle tests/smoke_instruction_cycle.cpp)
target_link_libraries(time-smoke-instruction-cycle time-gameboy-interpreter-core)
add_test(NAME smoke_instruction_cycle COMMAND time-smoke-instruction-cycle)
add_executable(time-smoke-executor tests/smoke_executor.cpp)
target_link_libraries(time-smoke-executor time-gameboy-interpreter-core)
add_test(NAME smoke_executor COMMAND time-smoke-executor)
add_executable(time-smoke-plugin-executor tests/smoke_plugin_executor.cpp)
target_link_libraries(time-smoke-plugin-executor time-gameboy-interpreter-core)
add_test(NAME smoke_plugin_executor COMMAND time-smoke-plugin-executor)
add_executable(time-smoke-plugin-abi tests/smoke_plugin_abi.cpp)
target_link_libraries(time-smoke-plugin-abi time-gameboy-interpreter-core)
add_test(NAME smoke_plugin_abi COMMAND time-smoke-plugin-abi)
add_executable(time-smoke-plugin-io tests/smoke_plugin_io.cpp)
target_link_libraries(time-smoke-plugin-io time-gameboy-interpreter-core)
add_test(NAME smoke_plugin_io COMMAND time-smoke-plugin-io)
add_executable(time-smoke-sdl-frontend-plugin tests/smoke_sdl_frontend_plugin.cpp)
target_link_libraries(time-smoke-sdl-frontend-plugin time-gameboy-interpreter-core time-sdl-frontend-loader)
add_test(NAME smoke_sdl_frontend_plugin COMMAND time-smoke-sdl-frontend-plugin)
add_executable(time-smoke-sdl-frontend-gamegear tests/smoke_sdl_frontend_gamegear.cpp)
target_link_libraries(time-smoke-sdl-frontend-gamegear time-gamegear-core time-sdl-frontend-loader)
add_test(NAME smoke_sdl_frontend_gamegear COMMAND time-smoke-sdl-frontend-gamegear)
add_executable(time-smoke-sdl-audio-transport tests/smoke_sdl_audio_transport.cpp)
target_link_libraries(time-smoke-sdl-audio-transport time-gameboy-interpreter-core time-sdl-frontend-loader)
add_test(NAME smoke_sdl_audio_transport COMMAND time-smoke-sdl-audio-transport)
add_executable(time-smoke-sdl-audio-resampler tests/smoke_sdl_audio_resampler.cpp)
target_link_libraries(time-smoke-sdl-audio-resampler time-gameboy-interpreter-core)
add_test(NAME smoke_sdl_audio_resampler COMMAND time-smoke-sdl-audio-resampler)
add_executable(time-smoke-audio-engine tests/smoke_audio_engine.cpp)
target_link_libraries(time-smoke-audio-engine time-gameboy-interpreter-core)
add_test(NAME smoke_audio_engine COMMAND time-smoke-audio-engine)
add_executable(time-smoke-input-engine tests/smoke_input_engine.cpp)
add_test(NAME smoke_input_engine COMMAND time-smoke-input-engine)
add_executable(time-smoke-input-service tests/smoke_input_service.cpp)
add_test(NAME smoke_input_service COMMAND time-smoke-input-service)
add_executable(time-smoke-input-transport tests/smoke_input_transport.cpp)
target_link_libraries(time-smoke-input-transport time-gameboy-interpreter-core time-sdl-frontend-loader)
add_test(NAME smoke_input_transport COMMAND time-smoke-input-transport)
add_executable(time-smoke-input-fallback tests/smoke_input_fallback.cpp)
add_test(NAME smoke_input_fallback COMMAND time-smoke-input-fallback)
add_executable(time-smoke-emulator-config tests/smoke_emulator_config.cpp)
target_link_libraries(time-smoke-emulator-config time-emulator-config)
add_test(NAME smoke_emulator_config COMMAND time-smoke-emulator-config)
add_executable(time-smoke-machine-factory tests/smoke_machine_factory.cpp)
target_link_libraries(time-smoke-machine-factory time-machine-factory)
add_test(NAME smoke_machine_factory COMMAND time-smoke-machine-factory)
add_executable(time-smoke-emulator-host tests/smoke_emulator_host.cpp)
target_link_libraries(time-smoke-emulator-host time-emulator-host)
add_test(NAME smoke_emulator_host COMMAND time-smoke-emulator-host)
add_executable(time-smoke-video-engine tests/smoke_video_engine.cpp)
target_link_libraries(time-smoke-video-engine time-gameboy-interpreter-core)
add_test(NAME smoke_video_engine COMMAND time-smoke-video-engine)
add_executable(time-smoke-video-service tests/smoke_video_service.cpp)
target_link_libraries(time-smoke-video-service time-gameboy-interpreter-core)
add_test(NAME smoke_video_service COMMAND time-smoke-video-service)
add_executable(time-smoke-video-transport tests/smoke_video_transport.cpp)
target_link_libraries(time-smoke-video-transport time-gameboy-interpreter-core time-headless-video-adapter time-sdl-video-adapter)
add_test(NAME smoke_video_transport COMMAND time-smoke-video-transport)
add_executable(time-smoke-video-fallback tests/smoke_video_fallback.cpp)
target_link_libraries(time-smoke-video-fallback time-gameboy-interpreter-core)
add_test(NAME smoke_video_fallback COMMAND time-smoke-video-fallback)
add_executable(time-smoke-visual-override tests/smoke_visual_override.cpp)
target_link_libraries(time-smoke-visual-override time-gameboy-interpreter-core)
add_test(NAME smoke_visual_override COMMAND time-smoke-visual-override)
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
add_test(NAME smoke_audio_service COMMAND time-smoke-audio-service)
add_executable(time-smoke-lifecycle-epoch-barrier tests/smoke_lifecycle_epoch_barrier.cpp)
target_link_libraries(time-smoke-lifecycle-epoch-barrier time-gameboy-interpreter-core)
add_test(NAME smoke_lifecycle_epoch_barrier COMMAND time-smoke-lifecycle-epoch-barrier)
add_executable(time-smoke-lifecycle-coordinator tests/smoke_lifecycle_coordinator.cpp)
target_link_libraries(time-smoke-lifecycle-coordinator time-gameboy-interpreter-core)
add_test(NAME smoke_lifecycle_coordinator COMMAND time-smoke-lifecycle-coordinator)
add_executable(time-smoke-lifecycle-contract tests/smoke_lifecycle_contract.cpp)
target_link_libraries(time-smoke-lifecycle-contract time-gameboy-interpreter-core)
add_test(NAME smoke_lifecycle_contract COMMAND time-smoke-lifecycle-contract)
add_executable(time-smoke-audio-pipeline tests/smoke_audio_pipeline.cpp)
target_link_libraries(time-smoke-audio-pipeline time-gameboy-interpreter-core)
add_test(NAME smoke_audio_pipeline COMMAND time-smoke-audio-pipeline)
add_executable(time-smoke-sdl-audio-output-backend tests/smoke_sdl_audio_output_backend.cpp)
target_link_libraries(time-smoke-sdl-audio-output-backend time-gameboy-interpreter-core time-sdl-audio-output)
add_test(NAME smoke_sdl_audio_output_backend COMMAND time-smoke-sdl-audio-output-backend)
add_executable(time-smoke-dummy-audio-output tests/smoke_dummy_audio_output.cpp)
target_link_libraries(time-smoke-dummy-audio-output time-gameboy-interpreter-core time-dummy-audio-output)
add_test(NAME smoke_dummy_audio_output COMMAND time-smoke-dummy-audio-output)
add_executable(time-smoke-file-audio-output tests/smoke_file_audio_output.cpp)
target_link_libraries(time-smoke-file-audio-output time-gameboy-interpreter-core time-file-audio-output)
add_test(NAME smoke_file_audio_output COMMAND time-smoke-file-audio-output)
add_executable(time-smoke-sdl-audio-resampling-integration tests/smoke_sdl_audio_resampling_integration.cpp)
target_link_libraries(time-smoke-sdl-audio-resampling-integration time-gameboy-interpreter-core time-sdl-frontend-loader)
add_test(NAME smoke_sdl_audio_resampling_integration COMMAND time-smoke-sdl-audio-resampling-integration)
add_executable(time-smoke-apu-audio tests/smoke_apu_audio.cpp)
target_link_libraries(time-smoke-apu-audio time-gameboy-interpreter-core)
add_test(NAME smoke_apu_audio COMMAND time-smoke-apu-audio)
add_executable(time-smoke-machine-boot tests/smoke_machine_boot.cpp)
target_link_libraries(time-smoke-machine-boot time-gameboy-interpreter-core)
add_test(NAME smoke_machine_boot COMMAND time-smoke-machine-boot)
add_executable(time-smoke-gameboy-cartridge-save tests/smoke_gameboy_cartridge_save.cpp)
target_link_libraries(time-smoke-gameboy-cartridge-save time-gameboy-interpreter-core)
add_test(NAME smoke_gameboy_cartridge_save COMMAND time-smoke-gameboy-cartridge-save)
add_executable(time-smoke-memory-map tests/smoke_memory_map.cpp)
target_link_libraries(time-smoke-memory-map time-gameboy-interpreter-core)
add_test(NAME smoke_memory_map COMMAND time-smoke-memory-map)
add_executable(time-smoke-runtime-context tests/smoke_runtime_context.cpp)
target_link_libraries(time-smoke-runtime-context time-gameboy-interpreter-core)
add_test(NAME smoke_runtime_context COMMAND time-smoke-runtime-context)
add_executable(time-smoke-opcode-table tests/smoke_opcode_table.cpp)
target_link_libraries(time-smoke-opcode-table time-gameboy-interpreter-core)
add_test(NAME smoke_opcode_table COMMAND time-smoke-opcode-table)
add_executable(time-smoke-interpreter-math-i8 tests/smoke_interpreter_math_i8.cpp)
target_link_libraries(time-smoke-interpreter-math-i8 time-gameboy-interpreter-core)
add_test(NAME smoke_interpreter_math_i8 COMMAND time-smoke-interpreter-math-i8)
add_executable(time-smoke-trace-executor tests/smoke_trace_executor.cpp)
target_link_libraries(time-smoke-trace-executor time-gameboy-interpreter-core)
add_test(NAME smoke_trace_executor COMMAND time-smoke-trace-executor)

# Timing engine smoke test
add_executable(time-smoke-timing-engine tests/smoke_timing_engine.cpp)
target_link_libraries(time-smoke-timing-engine time-gameboy-interpreter-core)
add_test(NAME smoke_timing_engine COMMAND time-smoke-timing-engine)

add_executable(time-smoke-timing-service tests/smoke_timing_service.cpp)
target_link_libraries(time-smoke-timing-service time-gameboy-interpreter-core)
add_test(NAME smoke_timing_service COMMAND time-smoke-timing-service)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    get_property(BMMQ_ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(bmmq_target IN LISTS BMMQ_ALL_TARGETS)
        if (bmmq_target MATCHES "^time-smoke-")
            target_compile_options(${bmmq_target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
