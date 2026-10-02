# Target-independent link probe for the HarfBuzz ICU adapter symbol.
#
# Archives are passed in explicit link order (HarfBuzz, ICU i18n, ICU common)
# so one-pass static resolution is exercised exactly as a real consumer sees it.

cmake_minimum_required(VERSION 3.27)

foreach(required CXX_COMPILER PROBE_SOURCE PROBE_ARCHIVES OUTPUT_BINARY
                 HARFBUZZ_INCLUDE_ROOT ICU_INCLUDE_ROOT EXPECT_SUCCESS
                 EXPECTED_SYMBOL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ProbeHarfBuzzIcuLink requires ${required}")
    endif()
endforeach()

set(compile_flags -std=c++17 -DU_STATIC_IMPLEMENTATION)
if(DEFINED PROBE_SYSROOT AND NOT PROBE_SYSROOT STREQUAL "")
    list(APPEND compile_flags -isysroot "${PROBE_SYSROOT}")
endif()

file(REMOVE "${OUTPUT_BINARY}")
execute_process(
    COMMAND "${CXX_COMPILER}" ${compile_flags}
            "-I${HARFBUZZ_INCLUDE_ROOT}" "-I${ICU_INCLUDE_ROOT}"
            "${PROBE_SOURCE}" ${PROBE_ARCHIVES}
            -o "${OUTPUT_BINARY}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error)
set(probe_diagnostic "${probe_output}${probe_error}")

if(EXPECT_SUCCESS)
    if(NOT probe_result EQUAL 0)
        message(FATAL_ERROR
            "expected the HarfBuzz ICU probe to link, but it failed:\n${probe_diagnostic}")
    endif()
    if(NOT EXISTS "${OUTPUT_BINARY}")
        message(FATAL_ERROR "HarfBuzz ICU probe linked but produced no binary")
    endif()
    message(STATUS "harfbuzz icu link probe succeeded as expected")
else()
    if(probe_result EQUAL 0)
        message(FATAL_ERROR
            "expected the HarfBuzz ICU probe to FAIL, but it linked successfully")
    endif()
    if(NOT probe_diagnostic MATCHES "${EXPECTED_SYMBOL}")
        message(FATAL_ERROR
            "HarfBuzz ICU probe failed for an unrelated reason; expected '${EXPECTED_SYMBOL}':\n${probe_diagnostic}")
    endif()
    message(STATUS
        "harfbuzz icu link probe failed as expected on ${EXPECTED_SYMBOL}")
endif()
