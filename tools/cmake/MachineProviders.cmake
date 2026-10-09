if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(time-test-machine-provider MODULE tests/fixtures/test_machine_provider.c)
    target_include_directories(time-test-machine-provider PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(time-smoke-machine-provider tests/smoke_machine_provider.cpp)
    target_link_libraries(time-smoke-machine-provider PRIVATE time-machine-factory)
    add_test(NAME smoke-machine-provider COMMAND time-smoke-machine-provider $<TARGET_FILE:time-test-machine-provider>)
    set_tests_properties(smoke-machine-provider PROPERTIES TIMEOUT 90)
    add_test(NAME smoke-machine-provider-cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_machine_provider_cli.py
        $<TARGET_FILE:timeEmulator> $<TARGET_FILE:time-test-machine-provider>)
    set_tests_properties(smoke-machine-provider-cli PROPERTIES TIMEOUT 180)
    add_library(time-native-machine-runtime MODULE machine/providers/NativeMachineRuntimeModule.cpp)
    set_target_properties(time-native-mod-loader time-space-core-contract PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(time-native-machine-runtime PRIVATE time-gameboy-interpreter-core time-gamegear-core)
    # Keep core C++ symbols private: this is a C table, not a C++ shared-library ABI.
    set_property(TARGET time-native-machine-runtime APPEND_STRING PROPERTY LINK_FLAGS
        " -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/machine/providers/runtime.exports")
    set_property(TARGET time-native-machine-runtime APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/machine/providers/runtime.exports")
    add_executable(time-smoke-machine-runtime tests/smoke_machine_runtime.cpp)
    target_link_libraries(time-smoke-machine-runtime PRIVATE time-machine-factory)
    add_test(NAME smoke-machine-runtime COMMAND time-smoke-machine-runtime $<TARGET_FILE:time-native-machine-runtime>)
    add_dependencies(time-smoke-machine-runtime time-native-machine-runtime)
    set_tests_properties(smoke-machine-runtime PROPERTIES TIMEOUT 180)
    add_library(time-test-machine-runtime MODULE tests/fixtures/test_machine_runtime.c)
    target_include_directories(time-test-machine-runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(time-smoke-machine-runtime-rejection tests/smoke_machine_runtime_rejection.cpp)
    target_link_libraries(time-smoke-machine-runtime-rejection PRIVATE time-machine-factory)
    add_test(NAME smoke-machine-runtime-rejection COMMAND time-smoke-machine-runtime-rejection $<TARGET_FILE:time-test-machine-runtime>)
    add_dependencies(time-smoke-machine-runtime-rejection time-test-machine-runtime)
    set_tests_properties(smoke-machine-runtime-rejection PROPERTIES TIMEOUT 90)
    add_test(NAME smoke-machine-runtime-cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_machine_runtime_cli.py
        $<TARGET_FILE:timeEmulator> $<TARGET_FILE:time-native-machine-runtime>)
    set_tests_properties(smoke-machine-runtime-cli PROPERTIES TIMEOUT 180)
endif()
