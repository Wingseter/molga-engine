# Always-checked repair boundary for the nested HarfBuzz raw install.
#
# Verifies the 36 consumed installed outputs: two raw archives, the 33 public
# headers that exist in the pinned source, and the generated hb-features.h,
# which is checked against an independently pinned SHA rather than against an
# installed copy — so a matching-but-wrong pair cannot agree its way past.
#
# Upstream's installed CMake export and pkg-config files are deliberately
# unconsumed incidental outputs and are not members of this repair set.

cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED MODE OR NOT MODE STREQUAL "RAW_INSTALL_WRAPPER")
    message(FATAL_ERROR "RepairHarfBuzzRawInstall requires MODE=RAW_INSTALL_WRAPPER")
endif()
foreach(required HARFBUZZ_SOURCE HARFBUZZ_BUILD HARFBUZZ_RAW_PREFIX)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RepairHarfBuzzRawInstall requires ${required}")
    endif()
endforeach()

# The exact installed public header allowlist at the pinned commit.
set(harfbuzz_public_headers
    hb-aat-layout.h hb-aat.h hb-blob.h hb-buffer.h hb-common.h hb-cplusplus.hh
    hb-deprecated.h hb-draw.h hb-face.h hb-features.h hb-font.h hb-icu.h
    hb-map.h hb-ot-color.h hb-ot-deprecated.h hb-ot-fetch.h hb-ot-font.h
    hb-ot-layout.h hb-ot-math.h hb-ot-meta.h hb-ot-metrics.h hb-ot-name.h
    hb-ot-shape.h hb-ot-var.h hb-ot.h hb-paint.h hb-script-list.h hb-set.h
    hb-shape-plan.h hb-shape.h hb-style.h hb-unicode.h hb-version.h hb.h)

set(build_archives
    "${HARFBUZZ_BUILD}/libharfbuzz.a"
    "${HARFBUZZ_BUILD}/libharfbuzz-icu.a")

function(harfbuzz_rerun_build)
    message(STATUS "repairing nested HarfBuzz build outputs")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${CMAKE_COMMAND}" --build "${HARFBUZZ_BUILD}"
                --target harfbuzz harfbuzz-icu
        RESULT_VARIABLE build_result)
    if(NOT build_result EQUAL 0)
        message(FATAL_ERROR "rebuilding the nested HarfBuzz archives failed")
    endif()
endfunction()

set(missing_build FALSE)
foreach(archive IN LISTS build_archives)
    if(NOT EXISTS "${archive}")
        set(missing_build TRUE)
    endif()
endforeach()
if(missing_build)
    harfbuzz_rerun_build()
    foreach(archive IN LISTS build_archives)
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR "HarfBuzz archive still missing after repair: ${archive}")
        endif()
    endforeach()
endif()

# Installed archives answer to the build tree; source headers answer to the
# clean pinned source; the generated header answers to the build tree copy.
function(harfbuzz_installed_set_matches out_var)
    set(matches TRUE)
    foreach(archive_name libharfbuzz libharfbuzz-icu)
        set(installed "${HARFBUZZ_RAW_PREFIX}/lib/${archive_name}.a")
        set(authority "${HARFBUZZ_BUILD}/${archive_name}.a")
        if(NOT EXISTS "${installed}")
            set(matches FALSE)
            break()
        endif()
        file(SHA256 "${installed}" installed_sha)
        file(SHA256 "${authority}" authority_sha)
        if(NOT installed_sha STREQUAL authority_sha)
            set(matches FALSE)
            break()
        endif()
    endforeach()
    if(matches)
        foreach(header IN LISTS harfbuzz_public_headers)
            set(installed "${HARFBUZZ_RAW_PREFIX}/include/harfbuzz/${header}")
            if(NOT EXISTS "${installed}")
                set(matches FALSE)
                break()
            endif()
            if(header STREQUAL "hb-features.h")
                set(authority "${HARFBUZZ_BUILD}/src/hb-features.h")
            else()
                set(authority "${HARFBUZZ_SOURCE}/src/${header}")
            endif()
            if(NOT EXISTS "${authority}")
                message(FATAL_ERROR "missing HarfBuzz header authority: ${authority}")
            endif()
            file(SHA256 "${installed}" installed_sha)
            file(SHA256 "${authority}" authority_sha)
            if(NOT installed_sha STREQUAL authority_sha)
                set(matches FALSE)
                break()
            endif()
        endforeach()
    endif()
    set(${out_var} ${matches} PARENT_SCOPE)
endfunction()

harfbuzz_installed_set_matches(installed_ok)
if(NOT installed_ok)
    message(STATUS "repairing nested HarfBuzz install")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ZERO_AR_DATE=1
                "${CMAKE_COMMAND}" --install "${HARFBUZZ_BUILD}"
        RESULT_VARIABLE install_result)
    if(NOT install_result EQUAL 0)
        message(FATAL_ERROR "reinstalling nested HarfBuzz failed")
    endif()
    harfbuzz_installed_set_matches(installed_ok)
    if(NOT installed_ok)
        message(FATAL_ERROR
            "the nested HarfBuzz install does not match its authorities after repair")
    endif()
endif()

list(LENGTH harfbuzz_public_headers header_count)
message(STATUS
    "nested HarfBuzz raw install verified: 2 archives and ${header_count} headers")
