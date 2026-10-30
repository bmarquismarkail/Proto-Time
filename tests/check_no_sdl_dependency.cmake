if (NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(DEFINED READELF)
    # A host ldd cannot load a foreign executable. Inspect the target ELF's
    # declared dependencies; the native test retains its transitive ldd check.
    execute_process(COMMAND "${READELF}" -d "${EXECUTABLE}" OUTPUT_VARIABLE dependencies
                    ERROR_VARIABLE error RESULT_VARIABLE result)
else()
    execute_process(COMMAND ldd "${EXECUTABLE}" OUTPUT_VARIABLE dependencies
                    ERROR_VARIABLE error RESULT_VARIABLE result)
endif()
if (NOT result EQUAL 0)
    message(FATAL_ERROR "dependency inspection failed: ${error}")
endif()
if (dependencies MATCHES "SDL")
    message(FATAL_ERROR "timeEmulator has an SDL runtime dependency:\n${dependencies}")
endif()
