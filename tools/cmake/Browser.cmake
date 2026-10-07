if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND Python3_Interpreter_FOUND)
    add_library(time-browser-inspector STATIC
        machine/plugins/video/remote/Inspector.cpp machine/plugins/video/remote/HttpServer.cpp)
    target_link_libraries(time-browser-inspector PUBLIC time-space-analysis Threads::Threads)
    add_executable(time-browser machine/plugins/video/remote/main.cpp)
    target_link_libraries(time-browser PRIVATE time-browser-inspector)
    add_dependencies(time-browser time-netplay-source-binding)
    foreach(asset index.html inspector.css inspector.js)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/machine/plugins/video/remote/${asset}
            ${CMAKE_CURRENT_BINARY_DIR}/browser-assets/${asset} COPYONLY)
    endforeach()
    add_executable(time-smoke-browser-inspector tests/smoke_browser_inspector.cpp)
    target_link_libraries(time-smoke-browser-inspector PRIVATE time-browser-inspector)
    add_test(NAME smoke-browser-inspector COMMAND time-smoke-browser-inspector)
    add_test(NAME smoke-browser-cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_browser_cli.py $<TARGET_FILE:time-browser>)
    set_tests_properties(smoke-browser-cli PROPERTIES TIMEOUT 90)
endif()
