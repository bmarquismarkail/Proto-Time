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
endif()
