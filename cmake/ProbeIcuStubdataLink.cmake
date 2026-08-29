# Target-independent link probe for the ICU stubdata symbol.
#
# Deliberately does not use the imported targets: a broken imported-target
# definition must not be able to mask a broken archive. EXPECT_SUCCESS=OFF
# asserts the link fails AND that its diagnostic names the expected symbol,
# so an unrelated failure is not mistaken for the expected red result.

cmake_minimum_required(VERSION 3.27)

foreach(required CXX_COMPILER PROBE_SOURCE PROBE_ARCHIVES OUTPUT_BINARY
                 ICU_INCLUDE_ROOT EXPECT_SUCCESS EXPECTED_SYMBOL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ProbeIcuStubdataLink requires ${required}")
    endif()
endforeach()

set(compile_flags -std=c++17 -DU_STATIC_IMPLEMENTATION)
if(DEFINED PROBE_SYSROOT AND NOT PROBE_SYSROOT STREQUAL "")
    list(APPEND compile_flags -isysroot "${PROBE_SYSROOT}")
endif()

file(REMOVE "${OUTPUT_BINARY}")
execute_process(
    COMMAND "${CXX_COMPILER}" ${compile_flags}
            "-I${ICU_INCLUDE_ROOT}"
            "${PROBE_SOURCE}" ${PROBE_ARCHIVES}
            -o "${OUTPUT_BINARY}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error)
set(probe_diagnostic "${probe_output}${probe_error}")

if(EXPECT_SUCCESS)
    if(NOT probe_result EQUAL 0)
        message(FATAL_ERROR
            "expected the ICU stubdata probe to link, but it failed:\n${probe_diagnostic}")
    endif()
    if(NOT EXISTS "${OUTPUT_BINARY}")
        message(FATAL_ERROR "ICU stubdata probe linked but produced no binary")
    endif()
    message(STATUS "icu stubdata link probe succeeded as expected")
else()
    if(probe_result EQUAL 0)
        message(FATAL_ERROR
            "expected the ICU stubdata probe to FAIL, but it linked successfully")
    endif()
    if(NOT probe_diagnostic MATCHES "${EXPECTED_SYMBOL}")
        message(FATAL_ERROR
            "ICU stubdata probe failed for an unrelated reason; expected '${EXPECTED_SYMBOL}':\n${probe_diagnostic}")
    endif()
    message(STATUS
        "icu stubdata link probe failed as expected on ${EXPECTED_SYMBOL}")
endif()
